/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gr�ser
 *
 *  Przycisk PB0 - rozpocz�cie operacji zmiany ustawie� daty i czasu w RTC/przej�cie do kolejnego elementu daty lub czasu/zako�czenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naci�ni�cie = zwi�kszenie o 1 bie��cego elementu daty lub czasu
 *                 (wyj�cie poza zakres danej sk�adowej daty lub czasu powoduje jej wyzerowanie)
 *  
 *  Dioda LED1 PD7 zielona  - sygnalizacja dzia�ania urz�dzenia ci�g�ym �wieceniem, sygnalizacja innych zdarze� miganiem
 *  Dioda LED2 PD6 czerwona - sygnalizacja trwania operacji zapisu danych na kart� SD ci�g�ym �wieceniem w trakcie trwania tej operacji,
 *                            sygnalizacja innych zdarze� (g��wnie b��d�w) miganiem
 *  W dokumentacji znajduje si� dok�adny opis migni�� diod i ich znaczenia.
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"		/* Deklaracje z API FatFS'a */
#include "utils.h"
#include "rtc.h"
#include <util/delay.h>



#pragma region ZmienneStaleMakra

/// Rozmiar bufora (liczba 20-bajtowych element�w do przechowywania rekord�w o zdarzeniach).
#define BUFFER_SIZE 20

/// Przestrze� robocza FatFS, potrzebna dla ka�dego wolumenu
FATFS FatFs;

/// Obiekt (uchwyt do) pliku, potrzebny dla ka�dego otwartego pliku
FIL Fil;

/**
 * Etap operacji zmiany ustawie� daty i czasu w RTC.<br>
 * Warto�� -1 oznacza tryb normalny, warto�ci od 0 do 5 to okre�lanie warto�ci kolejnych element�w daty i czasu, warto�� 6 to oczekiwanie na potwierdzenie
 * b�d� anulowanie zmiany ustawie� daty i czasu w RTC.
 */
int8_t set_rtc = -1;

/// Determinuje czy zmiana ustawie� daty i godziny zosta�a anulowana czy nie.
uint8_t set_rtc_cancelled = 0;

/*
 * Warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [5] (z pomini�ciem dni tygodnia), jakie maj� zosta� ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawie�.
 */
uint8_t set_rtc_values[6];

/// Przechowuje dat� i czas pobrane z RTC.
time now;

/* Flagi b��d�w i bie��cego stanu wybranych element�w urz�dzenia. */
volatile flags device_flags = {0, 0, 0, 0, 0, 0, 1, 0};

/// Bufor przechowuj�cy do 20 rekord�w informacyjnych o zarejestrowanych zdarzeniach.
char buffer[BUFFER_SIZE][20] = {{0,},};

/// Przechowuje indeks elementu bufora, do kt�rego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu.
volatile uint8_t buffer_index = 0;

/// Tablica nazw zdarze� wykrywanych przez urz�dzenie, u�ywana przy zapisie danych z bufora na kart� SD.
const char* events_names[6] = { "opened", "closed", "turned on", "no file system", "date time changed", "SD inserted" };

#pragma endregion ZmienneStaleMakra



/**
 * Pobiera bie��c� dat� i czas z RTC, zapisuje je wszystkie do pojedynczej warto�� typu DWORD i zwraca.<br>
 * Funkcja ta u�ywana jest przez FatFS.
 * @return Bie��c� dat� i czas, upakowane w warto�ci typu DWORD.
 */
DWORD get_fattime (void)
{
	DWORD current_time;
	
	RtcGetTime(&now);
	
	/* pakowanie daty i czasu do DWORD'a */
	current_time =
		((DWORD)(now.years + 20) << 25)
	  | ((DWORD) now.months << 21)
	  | ((DWORD) now.days << 16)
	  | ((DWORD) now.hours << 11)
	  | ((DWORD) now.minutes << 5)
	  | ((DWORD) now.seconds >> 1); /* minuty podawane s� z dok�adno�ci� do 30 sekund, dlatego przechowywane tu s� sekundy z zakresu 0 - 29 */
	
	return current_time;
}



/**
 * Zapisuje dane z bufora na kart� SD oraz przesuwa wska�nik bufora (buffer_index) na pocz�tek.<br>
 * W razie potrzeby ustawia flag� braku karty SD lub flag� b��du komunikacji z kart� SD.
 */
void SaveBuffer()
{
	/* zmienna iteracyjna */
	uint8_t i = 0;
	/* przechowuje ilo�� bajt�w zapisanych przez funkcj� f_write (u�ywana r�wnie� jako zmienna tymczasowa) */
	UINT bw = 0;
	/* tymczasowy bufor na dane do zapisania na karcie SD */
	char temp[38] = {'\0',};
	
	/* aby zapis danych nie zosta� przerwany */
	cli();
	
	/* pr�ba zamontowania systemu plik�w karty SD */
	switch(f_mount(&FatFs, "", 1))
	{
		/* je�li karta zg�asza swoj� niegotowo��, po 1 sekundzie nast�puje druga pr�ba zamontowania systemu plik�w */
		case FR_NOT_READY:
			_delay_ms(1000);
			
			/* je�li wci�� nie da si� zamontowa� systemu plik�w, nale�y powiadomi� u�ytkownika i zako�czy� dzia�anie funkcji */
			if(f_mount(&FatFs, "", 1) != FR_OK)
			{
				/* ustawienie flagi braku karty SD i flagi b��du komunikacji z kart� (dla odr�nienia, �e brak karty zosta� wykryty w tej funkcji) */
				if(!device_flags.no_sd_card)
					device_flags.sd_communication_error = device_flags.no_sd_card = 1;
				
				/* sekwencja migni�� diody czerwonej, sygnalizuj�ca u�ytkownikowi niegotowo�� karty SD */
				BlinkRed(5, 100, 100);
				
				break;
			}
		
		/* je�li pomy�lnie uda�o si� zamontowa� system FAT, nast�puje przej�cie do zapisu danych */
		case FR_OK:
			/* pr�ba otwarcia/utworzenia pliku, do kt�rego zapisywane s� informacje o wykrytych przez urz�dzenie zdarzeniach */
			if(f_open(&Fil, "DoorLog.txt", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK)
			{
				/* pr�ba ustawienia wska�nika w pliku na jego ko�cu */
				if(f_lseek(&Fil, f_size(&Fil)) == FR_OK)
				{
					/* o�wiecenie diody LED2 (czerwonej) */
					PORTD |= 1 << PD6;
	
					/* zapisanie na karcie SD rekord�w z bufora */
					for(i = 0; i < buffer_index; ++i)
					{
						/* skopiowanie daty, czasu i spacji, z uwzgl�dnieniem znaku \0 na potrzeby funkcji strcat */
						strncpy(temp, buffer[i], 18);
						temp[18] = '\0';
					
						/* skopiowanie nazwy zdarzenia */
						strcat(temp, events_names[(int)buffer[i][18]]);
							
						/* dodanie znaku nowej linii (CRLF) na ko�cu, z uwzgl�dnieniem znaku \0 na potrzeby funkcji f_write */
						bw = strlen(temp);
						temp[bw]     = '\r';
						temp[bw + 1] = '\n';
						temp[bw + 2] = '\0';
						
					
						/* pr�ba zapisu rekordu informacyjnego do pliku */
						if(f_write(&Fil, temp, strlen(temp), &bw) == FR_OK)
						{
							/* je�li zapisywany rekord dotyczy zmiany ustawie� daty i czasu w RTC, nast�pny rekord w buforze zawiera now� dat� i czas */
							if(buffer[i][18] == 4)
							{
								++i;
						
								/* dodanie znaku nowej linii (CRLF) na ko�cu */
								buffer[i][17] = '\r';
								buffer[i][18] = '\n';
									
								/* je�li pr�ba zapisu tych danych do pliku si� nie powiedzie */
								if(f_write(&Fil, buffer[i], strlen(buffer[i]), &bw) != FR_OK)
								{
									/* ustawienie flagi b��du komunikacji z kart� SD */
									device_flags.sd_communication_error = 1;
										
									/* je�li zapisano bezb��dnie co najmniej 1 element */
									if(i > 0)
									{
										/* przesuwanie zawarto�ci bufora do jego pocz�tku */
										bw = 0;
										
										for(; i < buffer_index; ++i)
										{
											strcpy(buffer[bw], buffer[i]);
											++bw;
										}
										
										/* aktualizowanie wska�nika bufora */
										buffer_index = bw + 1;
									}
										
									break;
								}
							}
						}
						else
						{
							/* ustawienie flagi b��du komunikacji z kart� SD */
							device_flags.sd_communication_error = 1;
								
							/* je�li zapisano bezb��dnie co najmniej 1 element */
							if(i > 0)
							{
								/* przesuwanie zawarto�ci bufora do jego pocz�tku */
								bw = 0;
								
								for(; i < buffer_index; ++i)
								{
									strcpy(buffer[bw], buffer[i]);
									++bw;
								}
								
								/* aktualizowanie wska�nika bufora */
								buffer_index = bw + 1;
							}
								
							break;
						}
					}
	
					/* zgaszenie diody LED2 (czerwonej) */
					PORTD &= 191;
					
					/* je�li nie by�o b��du, ca�y bufor na pewno zosta� zapisany na karcie SD */
					if(!device_flags.sd_communication_error)
						/* ustawienie wska�nika bufora na pocz�tek */
						buffer_index = 0;
					
					/* pr�ba zamkni�cia pliku */
					if(f_close(&Fil) != FR_OK)
						device_flags.sd_communication_error = 1;
				}
				else
					/* ustawienie flagi b��du komunikacji z kart� SD */
					device_flags.sd_communication_error = 1;
			}
   			else
   				/* ustawienie flagi b��du komunikacji z kart� SD */
				device_flags.sd_communication_error = 1;

			/* b��d, kt�ry wyst�pi� podczas komunikacji z kart� SD, zg�aszany jest u�ytkownikowi poprzez odpowiedni� sekwencj� migni�� czerwonej diody */
			if(device_flags.sd_communication_error)
				BlinkRed(3, 200, 100);
			
			/* wyczyszczenie flagi braku karty SD (dla odr�nienia, �e w tej funkcji nast�pi� b��d komunikacji z kart�, a nie wykrycie braku karty) */
			device_flags.no_sd_card = 0;
			
			break;
		
		/* b��d przy pr�bie zamontowania systemu plik�w sygnalizowany jest jako brak karty SD */
		default:
			/* ustawienie flagi braku karty SD i flagi b��du komunikacji z kart� (dla odr�nienia, �e brak karty zosta� wykryty w tej funkcji) */
			if(!device_flags.no_sd_card)
				device_flags.sd_communication_error = device_flags.no_sd_card = 1;
	}
	
	/* pr�ba odmontowania systemu plik�w */
	if(f_mount(NULL, "", 1) != FR_OK)
	{
		/* sekwencja migni�� diod, sygnalizuj�ca u�ytkownikowi b��d podczas pr�by odmontowania systemu plik�w */
		BlinkBoth(3, 200, 100);
	}
	
	/* ponowne w��czenie przerwa�, je�li jest to mo�liwe */
	if(device_flags.interrupts)
		sei();
}



/**
 * Zapisuje we wskazywanym przez 'buffer_index' elemencie bufora rekord o zarejestrowanym przez urz�dzenie zdarzeniu.<br>
 * Je�eli bufor jest zape�niony, wymusza zapisanie jego zawarto�ci na karcie SD.<br>
 * W razie potrzeby ustawia flagi braku karty SD i zape�nienia bufora.
 * @param event Kod reprezentuj�cy rodzaj zdarzenia zarejestrowany przez urz�dzenie.<br>W dokumentacji urz�dzenia znajduje si� lista zdarze� wraz z kodami.
 */
void SaveEvent(char event)
{
	/* pobranie aktualnej daty i czasu z RTC */
	RtcGetTime(&now);
	
	/* je�li bufor jest ju� pe�ny, nale�y wymusi� zapis jego zawarto�ci na kart� SD */
	if(buffer_index >= BUFFER_SIZE)
	{
		buffer_index = BUFFER_SIZE;
		
		/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi b��d,
		 * urz�dzenie zasygnalizuje to jako zape�nienie bufora przy braku karty SD */
		device_flags.buffer_full = 1;
		
		/* zapisanie danych z bufora na kart� SD
		 * je�li wcze�niej zg�oszono brak karty, w razie jej wykrycia nale�y zapisa� informacj� o tym w buforze */
		if(device_flags.no_sd_card)
		{
			SaveBuffer();
			
			if(!device_flags.no_sd_card)
				SaveEvent(5);
		}
		else
			SaveBuffer();
		
		/* je�li wyst�pi� b��d zapisu, a bufor jest pe�ny, nale�y uniemo�liwi� zapisywanie kolejnych informacji do bufora, aby nie wyj�� poza zakres tej tablicy */
		if(buffer_index >= BUFFER_SIZE)
			device_flags.no_sd_card = 1;
		else
		{
			/* je�li nie wyst�pi� �aden b��d, nast�puje czyszczenie flagi pe�nego bufora */
			if(!device_flags.sd_communication_error)
				device_flags.buffer_full = 0;
			/* je�li podczas wykonywania funkcji SaveBuffer odkryto brak karty SD */
			else if(device_flags.no_sd_card)
			{
				/* zapisywanie w buforze rekordu informuj�cego o braku karty SD */
				sprintf(buffer[buffer_index], "%02d-%02d-%02d %02d:%02d:%02d %c", now.years, now.months, now.days,
				now.hours, now.minutes, now.seconds, 3);
				
				++buffer_index;
				
				/* je�li bufor wci�� nie jest pe�ny, urz�dzenie informowa� ma tylko o braku karty SD */
				if(buffer_index < BUFFER_SIZE)
					device_flags.buffer_full = 0;
			}
		}
		
		/* wyczyszczenie flagi b��du komunikacji z kart� SD */
		device_flags.sd_communication_error = 0;
	}
	
	/* je�li bufor jest pe�ny i brak karty SD, nast�puje utrata informacji */
	if(!device_flags.buffer_full || !device_flags.no_sd_card)
	{
		/* sprawdzenie obecno�ci mo�liwego do zamontowania systemu plik�w */
		if(f_mount(&FatFs, "", 1) != FR_OK)
		{
			/* je�li ju� wcze�niej stwierdzono brak karty SD, nie ma sensu dublowa� informacji w buforze */
			if(!device_flags.no_sd_card)
			{
				/* zapisywanie w buforze rekordu informuj�cego o braku karty SD */
				sprintf(buffer[buffer_index], "%02d-%02d-%02d %02d:%02d:%02d %c", now.years, now.months, now.days,
				now.hours, now.minutes, now.seconds, 3);
		
				++buffer_index;
			}
			
			/* ustawienie flagi braku karty SD */
			device_flags.no_sd_card = 1;
		
			/* zape�nienie bufora przy braku karty SD */
			if(buffer_index >= BUFFER_SIZE)
			{
				buffer_index = BUFFER_SIZE;
				
				device_flags.buffer_full = 1;
			}
			else
				device_flags.buffer_full = 0;
		}
		else
		{
			/* pr�ba odmontowania systemu plik�w */
			if(f_mount(NULL, "", 1) != FR_OK)
			{
				/* sekwencja migni�� diod, sygnalizuj�ca u�ytkownikowi b��d podczas pr�by odmontowania systemu plik�w */
				BlinkBoth(3, 200, 100);
				
				/* _delay_ms wy��cza przerwania - ponowne w��czenie przerwa�, je�li jest to mo�liwe */
				if(device_flags.interrupts)
					sei();
			}
		
			/* wyczyszczenie flagi */
			device_flags.no_sd_card = 0;
		}
	
		/* je�li bufor jest pe�ny i brak karty SD, nast�puje utrata informacji */
		if(!device_flags.buffer_full || !device_flags.no_sd_card)
		{
			/* zapisywanie w buforze daty i czasu z RTC oraz symbolu zdarzenia jako napis o formacie "YY-MM-DD HH:ii:SS c" */
			sprintf(buffer[buffer_index], "%02d-%02d-%02d %02d:%02d:%02d %c", now.years, now.months, now.days,
				now.hours, now.minutes, now.seconds, event);
	
			/* przy zegarze taktuj�cym z cz�st. 1 MHz, z preskalerem 1024, w ci�gu 30 sekund licznik naliczy prawie 29297,
			 * dlatego ustawi�em tutaj warto�� 65535 (max) - 29296, aby przy 29297-mej inkrementacji nast�pi�o przepe�nienie licznika, co wywo�a przerwanie */
			TCNT1 = 36239;
	
			/* w��czenie Timera/Countera 1, ustawienie jego preskalera na 1024 */
			TCCR1B = 1 << CS12 | 1 << CS10;
	
			++buffer_index;
		}
	}
}



/**
 * Obs�uga przerwa� z kontaktronu (PD3).<br>
 * Rejestrowanie zdarzenia otwarcia/zamkni�cia drzwi.
 * @param INT1_vect Wektor przerwania zewn�trznego INT1.
 */
ISR(INT1_vect)
{
	/* zablokowanie funkcji SaveBuffer mo�liwo�ci w��czania przerwa� */
	device_flags.interrupts = 0;
	
	/* odczekanie a� drgania zestyk�w ustan� */
	_delay_ms(80);
	
	/* zapisujemy zdarzenie tylko je�li obecny stan drzwi jest przeciwny do poprzedniego */
	if(device_flags.reed_switch == !(PIND & (1 << PIND3)))
	{
		/* zapisanie bie��cego stanu drzwi */
		device_flags.reed_switch = (PIND & (1 << PIND3)) ? 1 : 0;
	
		/* zapisanie do bufora rekordu o zdarzeniu */
	
		if(PIND & (1 << PIND3))	/* PD3 == 1 -> drzwi otwarte */
			SaveEvent(0);
		else					/* PD3 == 0 -> drzwi zamkni�te */
			SaveEvent(1);
	}
	
	/* umo�liwienie funkcji SaveBuffer w��czania przerwa� */
	device_flags.interrupts = 1;
}



/**
 * Obs�uga przerwa� z przycisk�w (PB2).<br>
 * Ustawianie daty i czasu w zegarze czasu rzeczywistego.
 * @param INT2_vect Wektor przerwania zewn�trznego INT2.
 */
ISR(INT2_vect)
{
	/* zablokowanie funkcji SaveBuffer mo�liwo�ci w��czania przerwa� */
	device_flags.interrupts = 0;
	
	/* wci�ni�to przycisk PB0 */
	if(!(PINB & 1))
	{
		/* przycisk PB1 nie jest wci�ni�ty */
		if(PINB & 2)
		{
			/* przej�cie do nast�pnej sk�adowej */
			++set_rtc;
			
			switch(set_rtc)
			{
				/* rozpocz�cie operacji zmiany daty i czasu w RTC/zako�czenie ustawiania wszystkich sk�adowych i oczekiwanie na anulowanie lub zatwierdzenie
				 * zmian sygnalizowane jest trzykrotnym szybkim migni�ciem zielonej diody */
				case 0:
				case 6:
					BlinkGreen(3, 100, 100);
				break;
				
				/* anulowanie/zatwierdzenie wprowadzonych zmian */
				case 7:
					/* je�li anulowano, zapisujemy do tablicy warto�ci domy�lne */
					if(set_rtc_cancelled)
					{
						RTCDefaultValues();
						set_rtc_cancelled = 0;
						
						/* sygnalizacja anulowania zmiany ustawie� */
						BlinkRed(3, 100, 100);
					}
					/* w przeciwnym razie wysy�amy nowe ustawienia do RTC */
					else
					{
						/* je�li w buforze brak miejsca na 2 rekordy + 1 na ew. informacj� o braku karty SD (mo�e zosta� zapisana wewn�trz funkcji SaveEvent),
						 * nale�y zapisa� zawarto�� bufora na kart� SD */
						if(buffer_index > BUFFER_SIZE - 3)
						{
							/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi b��d,
							 * urz�dzenie zasygnalizuje to jako zape�nienie bufora przy braku karty SD */
							device_flags.buffer_full = 1;
							
							/* zapisanie danych z bufora na kart� SD
							 * je�li wcze�niej zg�oszono brak karty, w razie jej wykrycia nale�y zapisa� informacj� o tym w buforze */
							 if(device_flags.no_sd_card)
							 {
								 SaveBuffer();
							 
								if(!device_flags.no_sd_card)
									SaveEvent(5);
							 }
							 else
								SaveBuffer();
							
							/* je�li wyst�pi� b��d zapisu, a bufor jest pe�ny, nale�y uniemo�liwi� zapisywanie kolejnych informacji do bufora, aby nie wyj�� poza zakres tej tablicy */
							if(buffer_index >= BUFFER_SIZE)
							{
								buffer_index = BUFFER_SIZE;
								
								device_flags.no_sd_card = 1;
							}
							else
							{
								/* je�li nie by�o b��du zapisu, nast�puje wyczyszczenie flagi pe�nego bufora przy braku karty SD */
								if(!device_flags.sd_communication_error)
									device_flags.buffer_full = 0;
								/* je�li podczas wykonywania funkcji SaveBuffer odkryto brak karty SD */
								else if(device_flags.no_sd_card)
								{
									/* zapisywanie w buforze rekordu informuj�cego o braku karty SD */
									SaveEvent(3);
									
									++buffer_index;
									
									/* je�li bufor wci�� nie jest pe�ny, urz�dzenie informowa� ma tylko o braku karty SD */
									if(buffer_index < BUFFER_SIZE)
										device_flags.buffer_full = 0;
								}
							}
							
							/* wyczyszczenie flagi b��du komunikacji z kart� SD */
							device_flags.sd_communication_error = 0;
						}
					
						if(buffer_index <= BUFFER_SIZE - 3)
						{
							/* zapisanie do bufora rekordu o zdarzeniu */
							SaveEvent(4);
						
							/* zapisywanie w buforze stringowej reprezentacji nowych ustawie� daty i czasu dla RTC, w formacie YY-MM-DD HH:ii:SS */
							sprintf(buffer[buffer_index], "%02d-%02d-%02d %02d:%02d:%02d", set_rtc_values[Years], set_rtc_values[Century_months], set_rtc_values[Days],
								set_rtc_values[Hours], set_rtc_values[Minutes], set_rtc_values[VL_seconds]);
						
							/* przesuni�cie wska�nika bufora o 1 pozycj� do przodu (normalnie robi to funkcja SaveEvent) */
							++buffer_index;
					
							/* zapisanie w RTC nowych ustawie� daty i czasu */
							RtcSetTime(set_rtc_values);
						
							/* czyszczenie flagi vl */
							device_flags.vl = 0;
						
							/* sygnalizacja wys�ania nowych ustawie� do RTC */
							BlinkGreen(1, 1500, 100);
						}
						else
						{
							/* op�nienie, aby nast�pi� zauwa�alny odst�p pomi�dzy migni�ciami z SaveBuffer i tymi tutaj */
							_delay_ms(300);
							
							/* sygnalizacja anulowania zmiany ustawie� */
							BlinkRed(3, 100, 100);
						}
					}
				
					/* zako�czenie ustawiania daty i godziny dla RTC */
					set_rtc = -1;
				break;
				
				/* sygnalizacja przej�cia do kolejnej sk�adowej */
				case 1:
				case 2:
				case 3:
				case 4:
				case 5:
					BlinkGreen(set_rtc, 200, 100);
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
		else if(set_rtc > -1 && set_rtc < 6)
		{
			/* pojedyncze wci�ni�cie przycisku to zwi�kszenie bie��cej sk�adowej o 1 */
			++set_rtc_values[set_rtc];
			
#pragma region KontrolaZakresuDatyICzasu

			switch(set_rtc)
			{
				case 0:							/* VL_seconds */
					if(set_rtc_values[VL_seconds] > 59)
					{
						set_rtc_values[VL_seconds] = 0;
						/* aby wiedzie� kiedy mign�� dwukrotnie */
						set_rtc_cancelled = 0xFF;
					}
				break;
				case 1:							/* Minutes */
					if(set_rtc_values[Minutes] > 59)
					{
						set_rtc_values[Minutes] = 0;
						set_rtc_cancelled = 0xFF;
					}
				break;
				case 2:							/* Hours */
					if(set_rtc_values[Hours] > 23)
					{
						set_rtc_values[Hours] = 0;
						set_rtc_cancelled = 0xFF;
					}
				break;
				case 3:							/* Days */
					/* miesi�ce z 31 dniami */
					if(set_rtc_values[Century_months] == 1 || set_rtc_values[Century_months] == 3 || set_rtc_values[Century_months] == 5 ||
					   set_rtc_values[Century_months] == 7 || set_rtc_values[Century_months] == 8 || set_rtc_values[Century_months] == 10 || set_rtc_values[Century_months] == 12)
					{
						if(set_rtc_values[Days] > 31)
						{
							set_rtc_values[Days] = 1;
							set_rtc_cancelled = 0xFF;
						}
					}
					/* luty */
					else if(set_rtc_values[Century_months] == 2)
					{
						/* w roku przest�pnym */
						if((set_rtc_values[Years] % 4 == 0))
						{
							if(set_rtc_values[Days] > 29)
							{
								set_rtc_values[3] = 1;
								set_rtc_cancelled = 0xFF;
							}
						}
						/* w roku nieprzest�pnym */
						else
						{
							if(set_rtc_values[Days] > 28)
							{
								set_rtc_values[Days] = 1;
								set_rtc_cancelled = 0xFF;
							}
						}
					}
					/* miesi�ce z 30 dniami */
					else
					{
						if(set_rtc_values[Days] > 30)
						{
							set_rtc_values[Days] = 1;
							set_rtc_cancelled = 0xFF;
						}
					}
				break;
				case 4:							/* Century_months */
					if(set_rtc_values[Century_months] > 12)
					{
						set_rtc_values[Century_months] = 1;
						set_rtc_cancelled = 0xFF;
					}
				break;
				case 5:							/* Years */
					if(set_rtc_values[Years] > 99)
					{
						set_rtc_values[Years] = 0;
						set_rtc_cancelled = 0xFF;
					}
				break;
			}

#pragma endregion KontrolaZakresuDatyICzasu
			
			/* sygnalizacja przekroczenia zakresu bie��cej sk�adowej */
			if(set_rtc_cancelled)
			{
				BlinkRed(2, 100, 100);
				set_rtc_cancelled = 0;
			}
			/* sygnalizacja inkrementacji bie��cej sk�adowej */
			else
				BlinkRed(1, 200, 50);
		}
	}
	
	/* umo�liwienie funkcji SaveBuffer w��czania przerwa� */
	device_flags.interrupts = 1;
}



/**
 * Obs�uga przerwa� z 16-bitowego licznika Timer/Counter1.<br>
 * Przepe�nienie licznika po naliczaniu od warto�ci startowej 36239 oznacza up�yw oko�o 30 sekund i powoduje zapis danych z bufora na karcie SD.
 * @param TIMER1_OVF_vect Wektor przerwania przy przepe�nieniu 16-bitowego licznika Timer/Counter1.
 */
ISR(TIMER1_OVF_vect)
{
	/* zablokowanie funkcji SaveBuffer mo�liwo�ci w��czania przerwa� */
	device_flags.interrupts = 0;
	
	/* Przerwania o wy�szych priorytetach mog� (po�rednio lub bezpo�rednio) wywo�a� SaveBuffer, a wtedy poni�szy kod nie ma racji bytu.
	 * Dlatego najpierw sprawdzamy czy w buforze s� dane do zapisania. */
	if(buffer_index > 0)
	{
		/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi b��d,
		 * urz�dzenie zasygnalizuje to jako zape�nienie bufora przy braku karty SD */
		device_flags.buffer_full = 1;
		
		/* zapisanie danych z bufora na kart� SD
		 * je�li wcze�niej zg�oszono brak karty, w razie jej wykrycia nale�y zapisa� informacj� o tym w buforze */
		if(device_flags.no_sd_card)
		{
			SaveBuffer();
			
			if(!device_flags.no_sd_card)
				SaveEvent(5);
		}
		else
			SaveBuffer();
		
		/* je�li wyst�pi� b��d zapisu, a bufor jest pe�ny, nale�y uniemo�liwi� zapisywanie kolejnych informacji do bufora, aby nie wyj�� poza zakres tej tablicy */
		if(buffer_index >= BUFFER_SIZE)
		{
			buffer_index = BUFFER_SIZE;
			
			device_flags.no_sd_card = 1;
		}
		else
		{
			
			/* je�li nie by�o b��du zapisu, nast�puje wyczyszczenie flagi pe�nego bufora przy braku karty SD */
			if(!device_flags.sd_communication_error)
				device_flags.buffer_full = 0;
			/* je�li podczas wykonywania funkcji SaveBuffer odkryto brak karty SD */
			else if(device_flags.no_sd_card)
			{
				/* zapisywanie w buforze rekordu informuj�cego o braku karty SD */
				SaveEvent(3);
				
				++buffer_index;
				
				/* je�li bufor wci�� nie jest pe�ny, urz�dzenie informowa� ma tylko o braku karty SD */
				if(buffer_index < BUFFER_SIZE)
					device_flags.buffer_full = 0;
			}
		}
		
		/* wy��czenie Timera/Countera 1 nast�puje tylko wtedy, gdy bufor zostanie opr�niony */
		if(buffer_index == 0)
			TCCR1B &= 250;
		
		/* wyczyszczenie flagi b��du komunikacji z kart� SD */
		device_flags.sd_communication_error = 0;
		
		/* ustawienie w liczniku warto�ci startowej */
		TCNT1 = 36239;
	}
	
	/* umo�liwienie funkcji SaveBuffer w��czania przerwa� */
	device_flags.interrupts = 1;
}



/// Funkcja g��wna programu.
int main(void)
{
	/************************************************************************/
	/*                     Inicjalizacja urz�dzenia                         */
	/************************************************************************/
	
	/* ustawienie warto�ci domy�lnych w tablicy ustawie� daty i godziny dla RTC */
	RTCDefaultValues();
	
	/* wy��czenie funkcji JTAG, aby m�c u�ywa� portu C jako zwyk�ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
#pragma region UstawieniaPrzerwan

	/* w��czenie przerwa� zewn�trznych INT1 i INT2 */
	GICR |= 1 << INT1 | 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj�cym jest ustawiona domy�lnie */

#pragma endregion UstawieniaPrzerwan
	
#pragma region UstawieniaPinow

	/* domy�lne warto�ci w rejestrach DDRX i PORTX to 0, wi�c poni�ej wpisywane s� tylko 1 (tam gdzie to potrzebne) */
	
	/* PB7(SCK)  wyj�ciowy (zegar dla karty SD)			}
       PB6(MISO) wej�ciowy (dane odbierane z karty SD)	} inicjalizacja w
       PB5(MOSI) wyj�ciowy (dane wysy�ane do karty SD)	} pliku sdmm.c
       PB4(SS)   wyj�ciowy (slave select)				}
       PB2(INT2) wej�ciowy (przerwania zewn�trzne wywo�ywane przyciskami)
       PB1       wej�ciowy (przycisk)
       PB0       wej�ciowy (przycisk)*/
	PORTB = 1 << PB2 | 1 << PB1 | 1 << PB0;
	
	/* PC1 (SDA) i PC0 (SCL) s� wykorzystywane przez TWI, wi�c w��czam wewn�trzne rezystory podci�gaj�ce */
	PORTC = 1 << PC1 | 1 << PC0;
	
	/* PD7 i PD6 wyj�ciowe (diody)
       PD3(INT1) wej�ciowy (przerwania zewn�trzne od kontaktronu) */
	DDRD = 1 << PD7 | 1 << PD6;
	PORTD = 1 << PD3;
	
#pragma endregion UstawieniaPinow
	
	/* o�wiecenie diody LED1 (zielonej) */
	PORTD |= 1 << PD7;
	
	/* zapisanie bie��cego stanu drzwi */
	device_flags.reed_switch = (PIND & (1 << PIND3)) ? 1 : 0;
	
	/* zapisanie informacji o w��czeniu urz�dzenia */
	SaveEvent(2);
	
#pragma region UstawieniaTimerCounter

	/* w��czenie przerwania przy przepe�nieniu licznika Timer/Counter1 (16-bit) */
	TIMSK = 1 << TOIE1;

#pragma endregion UstawieniaTimerCounter
	
	/* w��czenie przerwa� */
	sei();
	
	/************************************************************************/
	/*                       p�tla g��wna programu                          */
	/************************************************************************/
    for(;;)
    {
        /* flaga VL ustawiona => dioda zielona miga (ok. 0,5 Hz)
         * w przeciwnym razie => dioda zielona �wieci si� ci�gle */
		if(device_flags.vl)
			PORTD ^= 128;
		else
			PORTD |= 128;
		
		/* brak karty SD (i ew. bufor pe�ny) => dioda czerwona miga
		 *                w przeciwnym razie => dioda czerwona jest zgaszona */
		if(device_flags.no_sd_card || device_flags.buffer_full)
			PORTD ^= 64;
		else
			PORTD &= 191;
		
		/* no_sd_card										ok. 0,5 Hz
		 * buffer_full (b��d komunikacji z kart�)			ok. 1 Hz
		 * no_sd_card && buffer_full						ok. 2 Hz
		 * Poniewa� funkcja _delay_ms wy��cza przerwania, trzeba je w��cza� po zako�czeniu oczekiwania.
		 * Aby zminimalizowa� czas oczekiwania na obs�ug� przerwania (i w efekcie jak najcz�ciej umo�liwia� rejestracj� przerwania o tym samym priorytecie),
		 * odmierzanie czasu do zmiany stanu diod zosta�o podzielone na sekwencje 'delay' oczekiwa� po 50 ms. */
		if(device_flags.buffer_full)
		{
			/* odczekanie 250 ms */
			delay(5, 50);
			
			/* je�li obie flagi s� ustawione, dioda czerwona ma zmienia� stan co 250 ms,
			 * je�li tylko buffer_full jest ustawiona, dioda czerwona ma zmienia� stan co 500 ms */
			if(device_flags.no_sd_card)
				PORTD ^= 64;
			
			/* odczekanie 250 ms */
			delay(5, 50);
			
			/* zmiana stanu diody czerwonej */
			PORTD ^= 64;
			
			/* odczekanie 250 ms */
			delay(5, 50);
			
			/* zmiana po 250 ms je�li obie flagi ustawione */
			if(device_flags.no_sd_card)
				PORTD ^= 64;
			
			/* odczekanie 250 ms */
			delay(5, 50);
		}
		else
		{
			/* odczekanie 1000 ms */
			delay(20, 50);
		}
    }
}
