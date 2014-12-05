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
 *  Dioda LED1 PD7 - sygnalizacja dzia�ania urz�dzenia ci�g�ym �wieceniem
 *  Dioda LED2 PD6 - sygnalizacja trwania operacji zapisu danych na kart� SD ci�g�ym �wieceniem w trakcie trwania operacji
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdint-gcc.h>
#include "rtc.h"



#pragma region ZmienneStaleMakra

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
bool set_rtc_cancelled = false;

/// Przechowuje dat� i czas pobrane z RTC.
time now;

/// Rozmiar bufora (liczba 22-bajtowych element�w do przechowywania rekord�w o zdarzeniach).
const int8_t BUFFER_SIZE = 10;

/// Bufor przechowuj�cy do 10 rekord�w informacyjnych o zarejestrowanych zdarzeniach.
char buffer[BUFFER_SIZE][20];

/// Przechowuje indeks elementu bufora, do kt�rego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu.
uint8_t buffer_index = 0;

#pragma region DefinicjeNazwZdarzen

/// Nazwa zdarzenia otwarcia drzwi.
char event_name1[] PROGMEM = "opened";
/// Nazwa zdarzenia zamkni�cia drzwi.
char event_name2[] PROGMEM = "closed";
/// Nazwa zdarzenia w��czenia urz�dzenia.
char event_name3[] PROGMEM = "turned on";
/// Nazwa zdarzenia wykrycia karty SD.
char event_name4[] PROGMEM = "SD inserted";
/// Nazwa zdarzenia wykrycia braku systemu plik�w, kt�ry mo�na zamontowa�.
char event_name5[] PROGMEM = "no file system";
/// Nazwa zdarzenia b��du po��czenia z kart� SD.
char event_name6[] PROGMEM = "connection error";
/// Nazwa zdarzenia zmiany ustawie� daty i czasu w RTC.
char event_name7[] PROGMEM = "date time changed";

#pragma endregion DefinicjeNazwZdarzen

/// Tablica nazw zdarze� wykrywanych przez urz�dzenie, u�ywana przy zapisie danych z bufora na kart� SD.
PGM_P events_names[] PROGMEM = {event_name1, event_name2, event_name3, event_name4, event_name5, event_name6, event_name7};

/// Ustawia warto�ci domy�lne w tablicy ustawie� daty i godziny dla RTC.
#define RTCDefaultValues()
{
	set_rtc_values[VL_seconds] = 0;
	set_rtc_values[Minutes] = 0;
	set_rtc_values[Hours] = 0;
	set_rtc_values[Days] = 1;
	set_rtc_values[Century_months] = 1;
	set_rtc_values[Years] = 14;
}

#pragma endregion ZmienneStaleMakra



/**
 * Zapisuje we wskazywanym przez 'buffer_index' elemencie bufora rekord o zarejestrowanym przez urz�dzenie zdarzeniu.<br>
 * Je�eli bufor jest zape�niony, wymusza zapisanie jego zawarto�ci na karcie SD.
 * @param event Kod reprezentuj�cy rodzaj zdarzenia zarejestrowany przez urz�dzenie.
 * @see W dokumentacji urz�dzenia znajduje si� lista zdarze� wraz z kodami.
 */
void SaveEvent(char event)
{
	RtcGetTime(&now);
	
	if(buffer_index == BUFFER_SIZE)
		SaveBuffer();
	
	/* zapisywanie w buforze daty i czasu z RTC oraz symbolu zdarzenia jako napis o formacie "YY-MM-DD HH:ii:SS c" */
	sprintf(buffer[buffer_index], "%2d-%2d-%2d %2d:%2d:%2d %c", now.years, now.months, now.days,
		now.hours, now.minutes, now.seconds, event);
	
	/* w��czenie Timera/Countera 1, ustawienie jego preskalera na 1024 */
	TCCR1B = 1 << CS12 | 1 << CS10;
	
	/* przy zegarze taktuj�cym z cz�st. 1 MHz, z preskalerem 1024, w ci�gu 30 sekund licznik naliczy prawie 29297,
	 * dlatego ustawi�em tutaj warto�� 65535 (max) - 29296, aby przy 29297-mej inkrementacji nast�pi�o przepe�nienie licznika, co wywo�a przerwanie */
	TCNT1 = 36239;
	
	++buffer_index;
}



/// Zapisuje dane z bufora na kart� SD, czy�ci go oraz przesuwa wska�nik w buforze na pocz�tek.
void SaveBuffer()
{
	uint8_t i = 0;
	
	/* ta operacja nie mo�e zosta� przerwana */
	cli();
	
	/* TODO: jaka� inicjalizacja tej karty, mo�e montowanie FS itp. */
	
	/* o�wiecenie diody LED2 (czerwonej) */
	PORTD |= 1 << PD6;
	
	/* zapisujemy na karcie SD rekordy z bufora (zawarto�� niepustych element�w bufora) */
	for(i = 0; i < BUFFER_SIZE; ++i)
	{
		if(strlen(buffer[i]) > 0)
		{
			/* UNDONE: zapis na kart� SD napisu buffer[i], po zamianie kodu zdarzenia na jego nazw� */
			/* trzeba tutaj b�dzie sprawdza� czy kodem tym jest 6 i je�li tak, to po zapisie od razu przej�� do nast�pnego elementu i te� go zapisa� (nowa data i czas) */
			/* trzeba b�dzie u�ywa� tymczasowej tablicy albo mie� tylko wska�nik char*, wstawi� na miejsce kodu zdarzenia \0, zapisa� tak rekord ze spacj� na ko�cu,
			 * a potem tylko dopisa� pobran� z pami�ci programu nazw� zdarzenia i \n */
		
			/* wyczyszczenie elementu bufora */
			memset((void*)buffer[i], 0, 20);
		}
		else
			break;
	}
	
	/* zgaszenie diody LED2 (czerwonej) */
	PORTD &= 191;
	
	/* ustawienie wska�nika w buforze na pocz�tek */
	buffer_index = 0;
	
	/* ponowne w��czenie przerwa� */
	sei();
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
			set_rtc_cancelled = true;
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
						if((set_rtc_values[Years] % 4 == 0)
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
	/* zapisanie danych z bufora na kart� SD */
	SaveBuffer();
	
	/* wy��czenie Timera/Countera 1 */
	TCCR1B &= 250;
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