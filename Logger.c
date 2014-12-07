/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gräser
 *
 *  Przycisk PB0 - rozpoczêcie operacji zmiany ustawieñ daty i czasu w RTC/przejœcie do kolejnego elementu daty lub czasu/zakoñczenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naciœniêcie zwiêkszenie o 1 bie¿¹cego elementu daty/czasu (wciœniêcie i przytrzymanie to pojedyncze naciœniêcie)
 *                 wyjœcie poza zakres danej sk³adowej daty/czasu powoduje jej wyzerowanie
 *  
 *  Dioda LED1 PD7 zielona - sygnalizacja dzia³ania urz¹dzenia ci¹g³ym œwieceniem
 *  Dioda LED2 PD6 czerwona - sygnalizacja trwania operacji zapisu danych na kartê SD ci¹g³ym œwieceniem w trakcie trwania operacji
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"		/* Deklaracje z API FatFS'a */
#include "rtc.h"



/**
 * Pole bitowe przechowuj¹ce flagi m.in. b³êdów.
 * @field led1 Bie¿¹cy stan diody zielonej
 * @field led2 Bie¿¹cy stan diody czerwonej
 * @field vl Wartoœæ bitu VL z rejestru VL_seconds w RTC (wartoœæ 1 informuje o utraceniu dok³adnoœci pomiaru czasu)
 * @field no_sd_card Flaga braku mo¿liwego do zamontowania systemu plików
 * @field buffer_full Flaga zape³nienia bufora przy jednoczesnym braku karty SD lub flaga b³êdu zapisu danych na kartê SD
 * @field sd_communication_error Flaga b³êdu u¿ywana wewn¹trz funkcji {@link SaveBuffer}
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

/// Rozmiar bufora (liczba 22-bajtowych elementów do przechowywania rekordów o zdarzeniach).
#define BUFFER_SIZE 10

/// Przestrzeñ robocza FatFS potrzebna dla ka¿dego volumenu
FATFS FatFs;

/// Obiekt (uchwyt do) pliku potrzebny dla ka¿dego otwartego pliku
FIL Fil;

/**
 * Oznaczenie trybu pracy urz¹dzenia.<br>
 * Wartoœæ -1 oznacza tryb normalny, wartoœci od 0 do 5 to ustawianie kolejnych elementów daty i czasu w RTC, wartoœæ 6 to oczekiwanie na potwierdzenie
 * b¹dŸ anulowanie zmiany ustawieñ daty i czasu w RTC.
 */
int8_t set_rtc = -1;

/**
 * Wartoœci kolejnych rejestrów RTC, od VL_seconds [0] do Years [5] (z pominiêciem dni tygodnia), jakie maj¹ zostaæ ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawieñ.
 */
uint8_t set_rtc_values[6];

/// Determinuje czy zmiana ustawieñ daty i godziny zosta³a anulowana czy nie.
uint8_t set_rtc_cancelled = 0;

/// Przechowuje datê i czas pobrane z RTC.
time now;

/// Bufor przechowuj¹cy do 10 rekordów informacyjnych o zarejestrowanych zdarzeniach.
char buffer[BUFFER_SIZE][20] = {{0,},};

/// Przechowuje indeks elementu bufora, do którego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu.
uint8_t buffer_index = 0;

/// Tablica nazw zdarzeñ wykrywanych przez urz¹dzenie, u¿ywana przy zapisie danych z bufora na kartê SD.
const char* events_names[7] = { "opened", "closed", "turned on", "SD inserted", "no file system", "connection error", "date time changed" };

/// Flagi b³êdów i bie¿¹cego stanu diod (u¿ywane przy sekwencjach migniêæ).
flags device_flags;

/// Ustawia wartoœci domyœlne w tablicy ustawieñ daty i godziny dla RTC.
#define RTCDefaultValues() do{ set_rtc_values[VL_seconds] = 0; set_rtc_values[Minutes] = 0; set_rtc_values[Hours] = 0; set_rtc_values[Days] = 1; set_rtc_values[Century_months] = 1; set_rtc_values[Years] = 14; } while(0)

#pragma endregion ZmienneStaleMakra



/**
 * Pobiera bie¿¹c¹ datê i czas, pakuje je do pojedynczej wartoœæ typu DWORD i zwraca.<br>
 * Funkcja ta wywo³ywana jest przez FatFS.
 * @return Bie¿¹c¹ datê i czas, upakowane w wartoœci typu DWORD.
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
	  | ((DWORD) now.seconds >> 1); /* minuty podawane s¹ z dok³adnoœci¹ do 30 sekund, dlatego przechowywane tu s¹ sekundy z zakresu 0 - 29 */
	
	return current_time;
}



/// Zapisuje dane z bufora na kartê SD, czyœci go oraz przesuwa wskaŸnik w buforze na pocz¹tek.
void SaveBuffer()
{
	/* zmienna iteracyjna */
	uint8_t i = 0;
	/* przechowuje iloœæ bajtów zapisanych przez funkcjê f_write */
	UINT bw = 0;
	/* przechowuje rezultat dzia³ania funkcji f_write */
	FRESULT result = FR_OK;
	/* tymczasowy bufor na dane do zapisania na karcie SD */
	char temp[38] = {'\0',};
	
	/* ta operacja nie mo¿e zostaæ przerwana */
	cli();
	
	/* próba zamontowania systemu plików karty SD */
	switch(f_mount(&FatFs, "", 1))
	{
		/* jeœli karta zg³asza swoj¹ niegotowoœæ, po 1 sekundzie nastêpuje druga próba zamontowania systemu plików */
		case FR_NOT_READY:
			_delay_ms(1000);
			
			/* jeœli wci¹¿ nie da siê zamontowaæ systemu plików, nale¿y powiadomiæ u¿ytkownika i zakoñczyæ dzia³anie funkcji (w³¹czyæ z powrotem przerwania) */
			if(f_mount(&FatFs, "", 1) != FR_OK)
			{
				/* TODO: zamigaæ diodami dla FR_NOT_READY, zrestartowaæ odliczanie czasu do zapisu, ustawiæ flagê b³êdu */
				
				break;
			}
		
		/* jeœli pomyœlnie uda³o siê zamontowaæ system FAT, nastêpuje przejœcie do zapisu danych */
		case FR_OK:
			/* próba otwarcia/utworzenia pliku, do którego zapisywane s¹ informacje o wykrytych przez urz¹dzenie zdarzeniach */
			if(f_open(&Fil, "door-logger.txt", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK)
			{
				/* próba ustawienia wskaŸnika w pliku na jego koñcu */
				if(f_lseek(Fil, f_size(Fil)) == FR_OK)
				{
					/* oœwiecenie diody LED2 (czerwonej) */
					PORTD |= 1 << PD6;
	
					/* zapisujemy na karcie SD rekordy z bufora (zawartoœæ niepustych elementów bufora) */
					for(i = 0; i < BUFFER_SIZE; ++i)
					{
						if(strlen(buffer[i]) > 0)
						{
							/* skopiowanie daty, czasu i spacji */
							strncpy(temp, buffer[i], 18);
					
							/* skopiowanie nazwy zdarzenia */
							strcpy(temp, events_names[buffer[i][18]]);
							
							/* dodanie znaku nowej linii na koñcu */
							temp[strlen(temp)] = '\n';
					
							/* próba zapisu rekordu informacyjnego do pliku */
							if((result = f_write(&Fil, temp, strlen(temp), &bw) == FR_OK))
							{
								/* jeœli zapisywany rekord dotyczy zmiany ustawieñ daty i czasu w RTC */
								if(buffer[i][18] == 6)
								{
									/* wyczyszczenie elementu bufora i bufora tymczasowego */
									memset((void*)buffer[i], 0, 20);
									memset((void*)temp, 0, 38);
						
									++i;
						
									/* skopiowanie nowej daty i czasu, zapisanych w RTC */
									strcpy(temp, buffer[i]);
									
									/* dodanie znaku nowej linii na koñcu */
									temp[17] = '\n';
									
									/* próba zapisu tych danych do pliku */
									if((result = f_write(&Fil, temp, strlen(temp), &bw) != FR_OK))
									{
										/* TODO: dosun¹æ bufor do lewej */
										
										break;
									}
								}
		
								/* wyczyszczenie elementu bufora i bufora tymczasowego */
								memset((void*)buffer[i], 0, 20);
								memset((void*)temp, 0, 38);
							}
							else
							{
								/* TODO: dosun¹æ bufor do lewej */
								
								break;
							}
						}
						else
							break;
					}
	
					/* zgaszenie diody LED2 (czerwonej) */
					PORTD &= 191;
	
					/* ustawienie wskaŸnika w buforze na pocz¹tek */
					buffer_index = 0;
				}
				
				/* próba zamkniêcia pliku */
				if(f_close(Fil) == FR_OK && result == FR_OK)
					/* aby nie pisaæ 5 razy tego samego migania diodami, równie¿ z tego case'a mo¿e nast¹piæ przejœcie do default'a,
					 * jeœli wyst¹pi b³¹d w jednej z 4 funkcji: f_open, f_seek, f_write lub f_close (taki zbiorczy else i default zarazem) */
					break;
			}
		
		/* wszelkie b³êdy przy próbie zamontowania systemu plików zg³aszane s¹ u¿ytkownikowi poprzez odpowiedni¹ sekwencjê migniêæ diod */
		default:
			/* TODO: zamigaæ diodami dla b³êdu z kart¹ SD, ustawiæ flagê b³êdu */
	}
	
	/* próba odmontowania systemu plików */
	if(f_mount(NULL, "", 1) != FR_OK)
		/* TODO: zamigaæ diodami dla b³êdu przy odmontowywaniu systemu plików */
	
	/* ponowne w³¹czenie przerwañ */
	sei();
}



/**
 * Zapisuje we wskazywanym przez 'buffer_index' elemencie bufora rekord o zarejestrowanym przez urz¹dzenie zdarzeniu.<br>
 * Je¿eli bufor jest zape³niony, wymusza zapisanie jego zawartoœci na karcie SD.
 * @param event Kod reprezentuj¹cy rodzaj zdarzenia zarejestrowany przez urz¹dzenie.
 * @see W dokumentacji urz¹dzenia znajduje siê lista zdarzeñ wraz z kodami.
 */
void SaveEvent(char event)
{
	RtcGetTime(&now);
	
	/* TODO: sprawdzaæ obecnoœæ karty SD, jeœli brak, to ustawiæ flagê i w³¹czyæ licznik */
	
	if(buffer_index == BUFFER_SIZE)
		SaveBuffer();
		/* TODO: jeœli flaga b³êdu = 1, to ustawiæ flagê pe³nego bufora przy braku karty SD */
	
	/* zapisywanie w buforze daty i czasu z RTC oraz symbolu zdarzenia jako napis o formacie "YY-MM-DD HH:ii:SS c" */
	sprintf(buffer[buffer_index], "%2d-%2d-%2d %2d:%2d:%2d %c", now.years, now.months, now.days,
		now.hours, now.minutes, now.seconds, event);
	
	/* przy zegarze taktuj¹cym z czêst. 1 MHz, z preskalerem 1024, w ci¹gu 30 sekund licznik naliczy prawie 29297,
	 * dlatego ustawi³em tutaj wartoœæ 65535 (max) - 29296, aby przy 29297-mej inkrementacji nast¹pi³o przepe³nienie licznika, co wywo³a przerwanie */
	TCNT1 = 36239;
	
	/* w³¹czenie Timera/Countera 1, ustawienie jego preskalera na 1024 */
	TCCR1B = 1 << CS12 | 1 << CS10;
	
	++buffer_index;
}



/**
 * Obs³uga przerwañ z kontaktronu (PD3).<br>
 * Rejestrowanie zdarzenia otwarcia/zamkniêcia drzwi.
 * @param INT1_vect Wektor przerwania zewnêtrznego INT1.
 */
ISR(INT1_vect)
{
	/* zapisanie do bufora rekordu o zdarzeniu */
	
	if(PIND & (1 << PD3))	/* PD3 == 1 -> drzwi otwarte */
		SaveEvent(0);
	else					/* PD3 == 0 -> drzwi zamkniête */
		SaveEvent(1);
}



/**
 * Obs³uga przerwañ z przycisków (PB2).<br>
 * Ustawianie daty i czasu w zegarze czasu rzeczywistego.
 * @param INT2_vect Wektor przerwania zewnêtrznego INT2.
 */
ISR(INT2_vect)
{
	/* wciœniêto przycisk PB0 */
	if(!(PINB & 1))
	{
		/* przycisk PB1 nie jest wciœniêty */
		if(PINB & 2)
		{
			/* jeœli pozosta³y jeszcze jakieœ sk³adowe daty/czasu do ustawienia */
			if(set_rtc < 6)
			{
				/* przejœcie do nastêpnej sk³adowej */
				++set_rtc;
			}
			/* jeœli nie, to urz¹dzenie oczekuje zatwierdzenia lub anulowania zmian */
			else
			{
				/* jeœli anulowano, zapisujemy do tablicy wartoœci domyœlne */
				if(set_rtc_cancelled)
				{
					RTCDefaultValues();
					
					set_rtc_cancelled = 0;
				}
				/* w przeciwnym razie wysy³amy nowe ustawienia do RTC */
				else
				{
					/* jeœli w buforze brak miejsca na 2 rekordy, nale¿y zapisaæ jego zawartoœæ na kartê SD */
					if(buffer_index > BUFFER_SIZE - 2)
					{
						SaveBuffer();
					}
					
					/* zapisanie do bufora rekordu o zdarzeniu */
					SaveEvent(6);
						
					/* zapisywanie w buforze stringowej reprezentacji nowych ustawieñ daty i czasu dla RTC, w formacie YY-MM-DD HH:ii:SS */
					sprintf(buffer[buffer_index], "%2d-%2d-%2d %2d:%2d:%2d", set_rtc_values[Years], set_rtc_values[Century_months], set_rtc_values[Days],
						set_rtc_values[Hours], set_rtc_values[Minutes], set_rtc_values[VL_seconds]);
						
					/* przesuniêcie wskaŸnika w buforze o 1 pozycjê do przodu (normalnie robi to funkcja SaveEvent) */
					++buffer_index;
					
					/* zapisanie w RTC nowych ustawieñ daty i godziny */
					RtcSetTime(set_rtc_values);
				}
				
				/* zakoñczenie ustawieñ daty i godziny dla RTC */
				set_rtc = -1;
			}
		}
	}
	/* wciœniêto przycisk PB1 i przycisk PB0 nie jest wciœniêty */
	else if(!(PINB & 2))
	{
		/* jeœli ustawiono ju¿ wszystkie sk³adowe daty i czasu, wciœniêcie tego przycisku oznacza anulowanie ustawieñ */
		if(set_rtc == 6)
		{
			set_rtc_cancelled = 0xFF;
		}
		else
		{
			/* pojedyncze wciœniêcie przycisku to zwiêkszenie bie¿¹cej sk³adowej o 1 */
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
					/* miesi¹ce z 31 dniami */
					if(set_rtc_values[Century_months] == 1 || set_rtc_values[Century_months] == 3 || set_rtc_values[Century_months] == 5 ||
					   set_rtc_values[Century_months] == 7 || set_rtc_values[Century_months] == 8 || set_rtc_values[Century_months] == 10 || set_rtc_values[Century_months] == 12)
					{
						if(set_rtc_values[Days] > 31)
							set_rtc_values[Days] = 1;
					}
					/* luty */
					else if(set_rtc_values[Century_months] == 2)
					{
						/* w roku przestêpnym */
						if((set_rtc_values[Years] % 4 == 0))
						{
							if(set_rtc_values[Days] > 29)
								set_rtc_values[3] = 1;
						}
						/* w roku nieprzestêpnym */
						else
						{
							if(set_rtc_values[Days] > 28)
								set_rtc_values[Days] = 1;
						}
					}
					/* miesi¹ce z 30 dniami */
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
 * Obs³uga przerwañ z 16-bitowego licznika Timer/Counter1.<br>
 * Przepe³nienie licznika po naliczaniu od wartoœci startowej 36239 oznacza up³yw oko³o 30 sekund i powoduje zapis danych z bufora na karcie SD.
 * @param TIMER1_OVF_vect Wektor przerwania przy przepe³nieniu 16-bitowego licznika Timer/Counter1.
 */
ISR(TIMER1_OVF_vect)
{
	/* Mog³o teraz wyst¹piæ przerwanie o wy¿szym priorytecie lub takowe mog³oby byæ ju¿ obs³ugiwane gdy wyst¹pi³o przerwanie z TIMER1.
	 * Przerwania o wy¿szych priorytetach mog¹ (poœrednio lub bezpoœrednio) wywo³aæ SaveBuffer, a wtedy poni¿szy kod nie ma racji bytu.
	 * Dlatego najpierw sprawdzamy, czy licznik zawiera wartoœæ mniejsz¹ ni¿ startowa, co oznacza jego przepe³nienie. */
	if(TCNT1 < 36239)
	{
		/* wy³¹czenie Timera/Countera 1 */
		TCCR1B &= 250;
		
		/* zapisanie danych z bufora na kartê SD */
		SaveBuffer();
	}
}



/// Funkcja g³ówna programu.
int main(void)
{
	/* zapisanie informacji o w³¹czeniu urz¹dzenia */
	SaveEvent(2);
	
	/* oœwiecenie diody LED1 (zielonej) */
	PORTD |= 1 << PD7;
	
	/************************************************************************/
	/*                     Inicjalizacja urz¹dzenia                         */
	/************************************************************************/
	
	/* wyzerowanie bufora */
	memset((void*)buffer, 0, BUFFER_SIZE * 20);
	/* ustawienie wartoœci domyœlnych w tablicy ustawieñ daty i godziny dla RTC */
	RTCDefaultValues();
	
	/* wy³¹czenie funkcji JTAG, aby móc u¿ywaæ portu C jako zwyk³ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
#pragma region UstawieniaPrzerwan

	/* w³¹czenie przerwañ zewnêtrznych INT1 i INT2 */
	GICR |= 1 << INT1;
	GICR |= 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj¹cym jest ustawiona domyœlnie */

#pragma endregion UstawieniaPrzerwan
	
#pragma region UstawieniaPinow

	/* domyœlne wartoœci w rejestrach DDRX i PORTX to 0, wpisujê wiêc tylko 1 tam, gdzie to potrzebne */
	
	/* PB7(SCK) wyjœciowy (zegar dla karty SD)
       PB6(MISO) wejœciowy (dane odbierane z karty SD)
       PB5(MOSI) wyjœciowy (dane wysy³ane do karty SD)
       PB4(SS) wyjœciowy (slave select)
       PB2(INT2) wejœciowy (przerwania zewnêtrzne wywo³ywane przyciskami)
       PB1 wejœciowy (przycisk)
       PB0 wejœciowy (przycisk)*/
	DDRB = 1 << PB7 | 1 << PB5 | 1 << PB4;
	PORTB = 1 << PB6 | 1 << PB2 | 1 << PB1 | 1 << PB0;
	
	/* PC1 (SDA) i PC0 (SCL) s¹ wykorzystywane przez TWI, wiêc w³¹czam wewnêtrzne rezystory podci¹gaj¹ce */
	PORTC = 1 << PC1 | 1 << PC0;
	
	/* PD7 i PD6 wyjœciowe (diody)
       PD3(INT1) wejœciowy (przerwania zewnêtrzne od kontaktronu) */
	DDRD = 1 << PD7 | 1 << PD6;
	PORTD = 1 << PD3;
	
#pragma endregion UstawieniaPinow
	
#pragma region UstawieniaTWI

	/* w³¹czam TWI (ustawienie bitu TWEN - TWI ENable)
	   TWCR - TWI Control Register */
	TWCR |= 1 << TWEA;
	
	/* ustawienie czêstotliwoœci dla TWI:
	   SCL frequency = CPU Clock frequency / (16 + 2(TWBR) * 4^TWPS)
	   dla TWBR = 0 i TWPS = 00 (wartoœci domyœlne) powy¿sze równanie da dzielnik równy 16
	   przy wewnêtrznym zegarze Atmegi taktuj¹cym z czêst. 1 MHz, otrzymam dla TWI czêst. 62,5 KHz
	   TWBR - TWI Bit rate Register
	   TWSR - TWI Status Register:
	       TWSR1:0 -> TWPS1, TWPS0 - TWI PreScaler bits */
	
#pragma endregion UstawieniaTWI

#pragma region UstawieniaTimerCounter1

	/* w³¹czenie przerwania przy przepe³nieniu 16-bitowego licznika Timera/Countera1 */
	TIMSK = 1 << TOIE1;

#pragma endregion UstawieniaTimerCounter1
	
	/* w³¹czenie przerwañ */
	sei();
	
	/************************************************************************/
	/*                       pêtla g³ówna programu                          */
	/************************************************************************/
    for(;;)
    {
        
    }
}