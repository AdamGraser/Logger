/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gr�ser
 *
 *  Przycisk PB0 - rozpocz�cie operacji zmiany ustawie� daty i czasu w RTC/przej�cie do kolejnego elementu daty lub czasu/zako�czenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naci�ni�cie zwi�kszenie o 1 bie��cego elementu daty/czasu (wci�ni�cie i przytrzymanie to pojedyncze naci�ni�cie)
 *                 wyj�cie poza zakres danej sk�adowej daty/czasu powoduje jej wyzerowanie
 *  
 *  Dioda LED1 PD7 zielona - sygnalizacja dzia�ania urz�dzenia ci�g�ym �wieceniem
 *  Dioda LED2 PD6 czerwona - sygnalizacja trwania operacji zapisu danych na kart� SD ci�g�ym �wieceniem w trakcie trwania operacji
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"		/* Deklaracje z API FatFS'a */
#include "rtc.h"



/**
 * Pole bitowe przechowuj�ce flagi m.in. b��d�w.
 * @field led1 Bie��cy stan diody zielonej
 * @field led2 Bie��cy stan diody czerwonej
 * @field vl Warto�� bitu VL z rejestru VL_seconds w RTC (warto�� 1 informuje o utraceniu dok�adno�ci pomiaru czasu)
 * @field no_sd_card Flaga braku mo�liwego do zamontowania systemu plik�w
 * @field buffer_full Flaga zape�nienia bufora przy jednoczesnym braku karty SD lub flaga b��du zapisu danych na kart� SD
 * @field sd_communication_error Flaga b��du u�ywana wewn�trz funkcji {@link SaveBuffer}
 */
typedef struct
{
	uint8_t led1:1,
			led2:1,
			vl:1,
			no_sd_card:1,
			buffer_full:1,
			sd_communication_error:3;
} flags = {0, 0, 0, 0, 0, 0};



#pragma region ZmienneStaleMakra

/// Rozmiar bufora (liczba 22-bajtowych element�w do przechowywania rekord�w o zdarzeniach).
#define BUFFER_SIZE 10

/// Przestrze� robocza FatFS potrzebna dla ka�dego volumenu
FATFS FatFs;

/// Obiekt (uchwyt do) pliku potrzebny dla ka�dego otwartego pliku
FIL Fil;

/**
 * Oznaczenie trybu pracy urz�dzenia.<br>
 * Warto�� -1 oznacza tryb normalny, warto�ci od 0 do 5 to ustawianie kolejnych element�w daty i czasu w RTC, warto�� 6 to oczekiwanie na potwierdzenie
 * b�d� anulowanie zmiany ustawie� daty i czasu w RTC.
 */
int8_t set_rtc = -1;

/**
 * Warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [5] (z pomini�ciem dni tygodnia), jakie maj� zosta� ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawie�.
 */
uint8_t set_rtc_values[6];

/// Determinuje czy zmiana ustawie� daty i godziny zosta�a anulowana czy nie.
uint8_t set_rtc_cancelled = 0;

/// Przechowuje dat� i czas pobrane z RTC.
time now;

/// Bufor przechowuj�cy do 10 rekord�w informacyjnych o zarejestrowanych zdarzeniach.
char buffer[BUFFER_SIZE][20] = {{0,},};

/// Przechowuje indeks elementu bufora, do kt�rego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu.
uint8_t buffer_index = 0;

/// Tablica nazw zdarze� wykrywanych przez urz�dzenie, u�ywana przy zapisie danych z bufora na kart� SD.
const char* events_names[7] = { "opened", "closed", "turned on", "SD inserted", "no file system", "connection error", "date time changed" };

/// Flagi b��d�w i bie��cego stanu diod (u�ywane przy sekwencjach migni��).
flags device_flags;

/// Ustawia warto�ci domy�lne w tablicy ustawie� daty i godziny dla RTC.
#define RTCDefaultValues() do{ set_rtc_values[VL_seconds] = 0; set_rtc_values[Minutes] = 0; set_rtc_values[Hours] = 0; set_rtc_values[Days] = 1; set_rtc_values[Century_months] = 1; set_rtc_values[Years] = 14; } while(0)

#pragma endregion ZmienneStaleMakra



/**
 * Pobiera bie��c� dat� i czas, pakuje je do pojedynczej warto�� typu DWORD i zwraca.<br>
 * Funkcja ta wywo�ywana jest przez FatFS.
 * @return Bie��c� dat� i czas, upakowane w warto�ci typu DWORD.
 */
DWORD get_fattime (void)
{
	DWORD current_time;
	
	RtcGetTime(&now);
	
	/* pakowanie daty i czasu do DWORD'a */
	current_time =
		((DWORD)(now.years + 2000 - 1980) << 25)
	  | ((DWORD) now.months << 21)
	  | ((DWORD) now.days << 16)
	  | ((DWORD) now.hours << 11)
	  | ((DWORD) now.minutes << 5)
	  | ((DWORD) now.seconds >> 1); /* minuty podawane s� z dok�adno�ci� do 30 sekund, dlatego przechowywane tu s� sekundy z zakresu 0 - 29 */
	
	return current_time;
}



/// Zapisuje dane z bufora na kart� SD, czy�ci go oraz przesuwa wska�nik w buforze na pocz�tek.
void SaveBuffer()
{
	/* zmienna iteracyjna */
	uint8_t i = 0;
	/* przechowuje ilo�� bajt�w zapisanych przez funkcj� f_write */
	UINT bw = 0;
	/* przechowuje rezultat dzia�ania funkcji f_write */
	FRESULT result = FR_OK;
	/* tymczasowy bufor na dane do zapisania na karcie SD */
	char temp[38] = {'\0',};
	
	/* ta operacja nie mo�e zosta� przerwana */
	cli();
	
	/* pr�ba zamontowania systemu plik�w karty SD */
	switch(f_mount(&FatFs, "", 1))
	{
		/* je�li karta zg�asza swoj� niegotowo��, po 1 sekundzie nast�puje druga pr�ba zamontowania systemu plik�w */
		case FR_NOT_READY:
			_delay_ms(1000);
			
			/* je�li wci�� nie da si� zamontowa� systemu plik�w, nale�y powiadomi� u�ytkownika i zako�czy� dzia�anie funkcji (w��czy� z powrotem przerwania) */
			if(f_mount(&FatFs, "", 1) != FR_OK)
			{
				/* TODO: zamiga� diodami dla FR_NOT_READY, zrestartowa� odliczanie czasu do zapisu, ustawi� flag� b��du */
				
				break;
			}
		
		/* je�li pomy�lnie uda�o si� zamontowa� system FAT, nast�puje przej�cie do zapisu danych */
		case FR_OK:
			/* pr�ba otwarcia/utworzenia pliku, do kt�rego zapisywane s� informacje o wykrytych przez urz�dzenie zdarzeniach */
			if(f_open(&Fil, "door-logger.txt", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK)
			{
				/* pr�ba ustawienia wska�nika w pliku na jego ko�cu */
				if(f_lseek(Fil, f_size(Fil)) == FR_OK)
				{
					/* o�wiecenie diody LED2 (czerwonej) */
					PORTD |= 1 << PD6;
	
					/* zapisujemy na karcie SD rekordy z bufora (zawarto�� niepustych element�w bufora) */
					for(i = 0; i < BUFFER_SIZE; ++i)
					{
						if(strlen(buffer[i]) > 0)
						{
							/* skopiowanie daty, czasu i spacji */
							strncpy(temp, buffer[i], 18);
					
							/* skopiowanie nazwy zdarzenia */
							strcpy(temp, events_names[buffer[i][18]]);
							
							/* dodanie znaku nowej linii na ko�cu */
							temp[strlen(temp)] = '\n';
					
							/* pr�ba zapisu rekordu informacyjnego do pliku */
							if((result = f_write(&Fil, temp, strlen(temp), &bw) == FR_OK))
							{
								/* je�li zapisywany rekord dotyczy zmiany ustawie� daty i czasu w RTC */
								if(buffer[i][18] == 6)
								{
									/* wyczyszczenie elementu bufora i bufora tymczasowego */
									memset((void*)buffer[i], 0, 20);
									memset((void*)temp, 0, 38);
						
									++i;
						
									/* skopiowanie nowej daty i czasu, zapisanych w RTC */
									strcpy(temp, buffer[i]);
									
									/* dodanie znaku nowej linii na ko�cu */
									temp[17] = '\n';
									
									/* pr�ba zapisu tych danych do pliku */
									if((result = f_write(&Fil, temp, strlen(temp), &bw) != FR_OK))
									{
										/* TODO: dosun�� bufor do lewej */
										
										break;
									}
								}
		
								/* wyczyszczenie elementu bufora i bufora tymczasowego */
								memset((void*)buffer[i], 0, 20);
								memset((void*)temp, 0, 38);
							}
							else
							{
								/* TODO: dosun�� bufor do lewej */
								
								break;
							}
						}
						else
							break;
					}
	
					/* zgaszenie diody LED2 (czerwonej) */
					PORTD &= 191;
	
					/* ustawienie wska�nika w buforze na pocz�tek */
					buffer_index = 0;
				}
				
				/* pr�ba zamkni�cia pliku */
				if(f_close(Fil) == FR_OK && result == FR_OK)
					/* aby nie pisa� 5 razy tego samego migania diodami, r�wnie� z tego case'a mo�e nast�pi� przej�cie do default'a,
					 * je�li wyst�pi b��d w jednej z 4 funkcji: f_open, f_seek, f_write lub f_close (taki zbiorczy else i default zarazem) */
					break;
			}
		
		/* wszelkie b��dy przy pr�bie zamontowania systemu plik�w zg�aszane s� u�ytkownikowi poprzez odpowiedni� sekwencj� migni�� diod */
		default:
			/* TODO: zamiga� diodami dla b��du z kart� SD, ustawi� flag� b��du */
	}
	
	/* pr�ba odmontowania systemu plik�w */
	if(f_mount(NULL, "", 1) != FR_OK)
		/* TODO: zamiga� diodami dla b��du przy odmontowywaniu systemu plik�w */
	
	/* ponowne w��czenie przerwa� */
	sei();
}



/**
 * Zapisuje we wskazywanym przez 'buffer_index' elemencie bufora rekord o zarejestrowanym przez urz�dzenie zdarzeniu.<br>
 * Je�eli bufor jest zape�niony, wymusza zapisanie jego zawarto�ci na karcie SD.
 * @param event Kod reprezentuj�cy rodzaj zdarzenia zarejestrowany przez urz�dzenie.
 * @see W dokumentacji urz�dzenia znajduje si� lista zdarze� wraz z kodami.
 */
void SaveEvent(char event)
{
	RtcGetTime(&now);
	
	/* TODO: sprawdza� obecno�� karty SD, je�li brak, to ustawi� flag� i w��czy� licznik */
	
	if(buffer_index == BUFFER_SIZE)
		SaveBuffer();
		/* TODO: je�li flaga b��du = 1, to ustawi� flag� pe�nego bufora przy braku karty SD */
	
	/* zapisywanie w buforze daty i czasu z RTC oraz symbolu zdarzenia jako napis o formacie "YY-MM-DD HH:ii:SS c" */
	sprintf(buffer[buffer_index], "%2d-%2d-%2d %2d:%2d:%2d %c", now.years, now.months, now.days,
		now.hours, now.minutes, now.seconds, event);
	
	/* przy zegarze taktuj�cym z cz�st. 1 MHz, z preskalerem 1024, w ci�gu 30 sekund licznik naliczy prawie 29297,
	 * dlatego ustawi�em tutaj warto�� 65535 (max) - 29296, aby przy 29297-mej inkrementacji nast�pi�o przepe�nienie licznika, co wywo�a przerwanie */
	TCNT1 = 36239;
	
	/* w��czenie Timera/Countera 1, ustawienie jego preskalera na 1024 */
	TCCR1B = 1 << CS12 | 1 << CS10;
	
	++buffer_index;
}



/**
 * Obs�uga przerwa� z kontaktronu (PD3).<br>
 * Rejestrowanie zdarzenia otwarcia/zamkni�cia drzwi.
 * @param INT1_vect Wektor przerwania zewn�trznego INT1.
 */
ISR(INT1_vect)
{
	/* zapisanie do bufora rekordu o zdarzeniu */
	
	if(PIND & (1 << PD3))	/* PD3 == 1 -> drzwi otwarte */
		SaveEvent(0);
	else					/* PD3 == 0 -> drzwi zamkni�te */
		SaveEvent(1);
}



/**
 * Obs�uga przerwa� z przycisk�w (PB2).<br>
 * Ustawianie daty i czasu w zegarze czasu rzeczywistego.
 * @param INT2_vect Wektor przerwania zewn�trznego INT2.
 */
ISR(INT2_vect)
{
	/* wci�ni�to przycisk PB0 */
	if(!(PINB & 1))
	{
		/* przycisk PB1 nie jest wci�ni�ty */
		if(PINB & 2)
		{
			/* je�li pozosta�y jeszcze jakie� sk�adowe daty/czasu do ustawienia */
			if(set_rtc < 6)
			{
				/* przej�cie do nast�pnej sk�adowej */
				++set_rtc;
			}
			/* je�li nie, to urz�dzenie oczekuje zatwierdzenia lub anulowania zmian */
			else
			{
				/* je�li anulowano, zapisujemy do tablicy warto�ci domy�lne */
				if(set_rtc_cancelled)
				{
					RTCDefaultValues();
					
					set_rtc_cancelled = 0;
				}
				/* w przeciwnym razie wysy�amy nowe ustawienia do RTC */
				else
				{
					/* je�li w buforze brak miejsca na 2 rekordy, nale�y zapisa� jego zawarto�� na kart� SD */
					if(buffer_index > BUFFER_SIZE - 2)
					{
						SaveBuffer();
					}
					
					/* zapisanie do bufora rekordu o zdarzeniu */
					SaveEvent(6);
						
					/* zapisywanie w buforze stringowej reprezentacji nowych ustawie� daty i czasu dla RTC, w formacie YY-MM-DD HH:ii:SS */
					sprintf(buffer[buffer_index], "%2d-%2d-%2d %2d:%2d:%2d", set_rtc_values[Years], set_rtc_values[Century_months], set_rtc_values[Days],
						set_rtc_values[Hours], set_rtc_values[Minutes], set_rtc_values[VL_seconds]);
						
					/* przesuni�cie wska�nika w buforze o 1 pozycj� do przodu (normalnie robi to funkcja SaveEvent) */
					++buffer_index;
					
					/* zapisanie w RTC nowych ustawie� daty i godziny */
					RtcSetTime(set_rtc_values);
				}
				
				/* zako�czenie ustawie� daty i godziny dla RTC */
				set_rtc = -1;
			}
		}
	}
	/* wci�ni�to przycisk PB1 i przycisk PB0 nie jest wci�ni�ty */
	else if(!(PINB & 2))
	{
		/* je�li ustawiono ju� wszystkie sk�adowe daty i czasu, wci�ni�cie tego przycisku oznacza anulowanie ustawie� */
		if(set_rtc == 6)
		{
			set_rtc_cancelled = 0xFF;
		}
		else
		{
			/* pojedyncze wci�ni�cie przycisku to zwi�kszenie bie��cej sk�adowej o 1 */
			++set_rtc_values[set_rtc];
			
#pragma region KontrolaZakresuDatyICzasu

			switch(set_rtc)
			{
				case 0:							/* VL_seconds */
					if(set_rtc_values[VL_seconds] > 59)
						set_rtc_values[VL_seconds] = 0;
				break;
				case 1:							/* Minutes */
					if(set_rtc_values[Minutes] > 59)
						set_rtc_values[Minutes] = 0;
				break;
				case 2:							/* Hours */
					if(set_rtc_values[Hours] > 23)
						set_rtc_values[Hours] = 0;
				break;
				case 3:							/* Days */
					/* miesi�ce z 31 dniami */
					if(set_rtc_values[Century_months] == 1 || set_rtc_values[Century_months] == 3 || set_rtc_values[Century_months] == 5 ||
					   set_rtc_values[Century_months] == 7 || set_rtc_values[Century_months] == 8 || set_rtc_values[Century_months] == 10 || set_rtc_values[Century_months] == 12)
					{
						if(set_rtc_values[Days] > 31)
							set_rtc_values[Days] = 1;
					}
					/* luty */
					else if(set_rtc_values[Century_months] == 2)
					{
						/* w roku przest�pnym */
						if((set_rtc_values[Years] % 4 == 0))
						{
							if(set_rtc_values[Days] > 29)
								set_rtc_values[3] = 1;
						}
						/* w roku nieprzest�pnym */
						else
						{
							if(set_rtc_values[Days] > 28)
								set_rtc_values[Days] = 1;
						}
					}
					/* miesi�ce z 30 dniami */
					else
					{
						if(set_rtc_values[Days] > 30)
							set_rtc_values[Days] = 1;
					}
				break;
				case 4:							/* Century_months */
					if(set_rtc_values[Century_months] > 12)
						set_rtc_values[Century_months] = 1;
				break;
				case 5:							/* Years */
					if(set_rtc_values[Years] > 99)
						set_rtc_values[Years] = 0;
				break;
			}

#pragma endregion KontrolaZakresuDatyICzasu

		}
	}
}



/**
 * Obs�uga przerwa� z 16-bitowego licznika Timer/Counter1.<br>
 * Przepe�nienie licznika po naliczaniu od warto�ci startowej 36239 oznacza up�yw oko�o 30 sekund i powoduje zapis danych z bufora na karcie SD.
 * @param TIMER1_OVF_vect Wektor przerwania przy przepe�nieniu 16-bitowego licznika Timer/Counter1.
 */
ISR(TIMER1_OVF_vect)
{
	/* Mog�o teraz wyst�pi� przerwanie o wy�szym priorytecie lub takowe mog�oby by� ju� obs�ugiwane gdy wyst�pi�o przerwanie z TIMER1.
	 * Przerwania o wy�szych priorytetach mog� (po�rednio lub bezpo�rednio) wywo�a� SaveBuffer, a wtedy poni�szy kod nie ma racji bytu.
	 * Dlatego najpierw sprawdzamy, czy licznik zawiera warto�� mniejsz� ni� startowa, co oznacza jego przepe�nienie. */
	if(TCNT1 < 36239)
	{
		/* wy��czenie Timera/Countera 1 */
		TCCR1B &= 250;
		
		/* zapisanie danych z bufora na kart� SD */
		SaveBuffer();
	}
}



/// Funkcja g��wna programu.
int main(void)
{
	/* zapisanie informacji o w��czeniu urz�dzenia */
	SaveEvent(2);
	
	/* o�wiecenie diody LED1 (zielonej) */
	PORTD |= 1 << PD7;
	
	/************************************************************************/
	/*                     Inicjalizacja urz�dzenia                         */
	/************************************************************************/
	
	/* wyzerowanie bufora */
	memset((void*)buffer, 0, BUFFER_SIZE * 20);
	/* ustawienie warto�ci domy�lnych w tablicy ustawie� daty i godziny dla RTC */
	RTCDefaultValues();
	
	/* wy��czenie funkcji JTAG, aby m�c u�ywa� portu C jako zwyk�ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
#pragma region UstawieniaPrzerwan

	/* w��czenie przerwa� zewn�trznych INT1 i INT2 */
	GICR |= 1 << INT1;
	GICR |= 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj�cym jest ustawiona domy�lnie */

#pragma endregion UstawieniaPrzerwan
	
#pragma region UstawieniaPinow

	/* domy�lne warto�ci w rejestrach DDRX i PORTX to 0, wpisuj� wi�c tylko 1 tam, gdzie to potrzebne */
	
	/* PB7(SCK) wyj�ciowy (zegar dla karty SD)
       PB6(MISO) wej�ciowy (dane odbierane z karty SD)
       PB5(MOSI) wyj�ciowy (dane wysy�ane do karty SD)
       PB4(SS) wyj�ciowy (slave select)
       PB2(INT2) wej�ciowy (przerwania zewn�trzne wywo�ywane przyciskami)
       PB1 wej�ciowy (przycisk)
       PB0 wej�ciowy (przycisk)*/
	DDRB = 1 << PB7 | 1 << PB5 | 1 << PB4;
	PORTB = 1 << PB6 | 1 << PB2 | 1 << PB1 | 1 << PB0;
	
	/* PC1 (SDA) i PC0 (SCL) s� wykorzystywane przez TWI, wi�c w��czam wewn�trzne rezystory podci�gaj�ce */
	PORTC = 1 << PC1 | 1 << PC0;
	
	/* PD7 i PD6 wyj�ciowe (diody)
       PD3(INT1) wej�ciowy (przerwania zewn�trzne od kontaktronu) */
	DDRD = 1 << PD7 | 1 << PD6;
	PORTD = 1 << PD3;
	
#pragma endregion UstawieniaPinow
	
#pragma region UstawieniaTWI

	/* w��czam TWI (ustawienie bitu TWEN - TWI ENable)
	   TWCR - TWI Control Register */
	TWCR |= 1 << TWEA;
	
	/* ustawienie cz�stotliwo�ci dla TWI:
	   SCL frequency = CPU Clock frequency / (16 + 2(TWBR) * 4^TWPS)
	   dla TWBR = 0 i TWPS = 00 (warto�ci domy�lne) powy�sze r�wnanie da dzielnik r�wny 16
	   przy wewn�trznym zegarze Atmegi taktuj�cym z cz�st. 1 MHz, otrzymam dla TWI cz�st. 62,5 KHz
	   TWBR - TWI Bit rate Register
	   TWSR - TWI Status Register:
	       TWSR1:0 -> TWPS1, TWPS0 - TWI PreScaler bits */
	
#pragma endregion UstawieniaTWI

#pragma region UstawieniaTimerCounter1

	/* w��czenie przerwania przy przepe�nieniu 16-bitowego licznika Timera/Countera1 */
	TIMSK = 1 << TOIE1;

#pragma endregion UstawieniaTimerCounter1
	
	/* w��czenie przerwa� */
	sei();
	
	/************************************************************************/
	/*                       p�tla g��wna programu                          */
	/************************************************************************/
    for(;;)
    {
        
    }
}