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
 *  Dioda LED1 PD7 zielona - sygnalizacja dzia�ania urz�dzenia ci�g�ym �wieceniem, sygnalizacja innych zdarze� miganiem
 *  Dioda LED2 PD6 czerwona - sygnalizacja trwania operacji zapisu danych na kart� SD ci�g�ym �wieceniem w trakcie trwania operacji,
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

/// Determinuje czy zmiana ustawie� daty i godziny zosta�a anulowana czy nie.
uint8_t set_rtc_cancelled = 0;

/*
 * Warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [5] (z pomini�ciem dni tygodnia), jakie maj� zosta� ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawie�.
 */
uint8_t set_rtc_values[6];

/// Przechowuje dat� i czas pobrane z RTC.
time now;

/* Flagi b��d�w i bie��cego stanu diod (u�ywane przy sekwencjach migni��). */
volatile flags device_flags = {0, 0, 0, 0, 0, 0, 1, 0};

/// Bufor przechowuj�cy do 10 rekord�w informacyjnych o zarejestrowanych zdarzeniach.
char buffer[BUFFER_SIZE][20] = {{0,},};

/// Przechowuje indeks elementu bufora, do kt�rego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu.
volatile uint8_t buffer_index = 0;

/// Tablica nazw zdarze� wykrywanych przez urz�dzenie, u�ywana przy zapisie danych z bufora na kart� SD.
const char* events_names[5] = { "opened", "closed", "turned on", "no file system", "date time changed" };

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
		((DWORD)(now.years + 20) << 25)
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
	/* przechowuje ilo�� bajt�w zapisanych przez funkcj� f_write
	 * (u�ywana r�wnie� jako zmienna tymczasowa) */
	UINT bw = 0;
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
				/* ustawienie flagi b��du komunikacji z kart� SD */
				device_flags.sd_communication_error = 1;
				
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
							/* je�li zapisywany rekord dotyczy zmiany ustawie� daty i czasu w RTC */
							if(buffer[i][18] == 4)
							{
								++i;
						
								/* dodanie znaku nowej linii (CRLF) na ko�cu */
								buffer[i][17] = '\r';
								buffer[i][18] = '\n';
									
								/* pr�ba zapisu tych danych do pliku */
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
									
// 									BlinkGreen(3, 200, 100);
// 									_delay_ms(300);
// 									BlinkRed(6, 200, 100);
										
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
							
// 							BlinkGreen(3, 200, 100);
// 							_delay_ms(300);
// 							BlinkRed(6, 200, 100);
								
							break;
						}
					}
	
					/* zgaszenie diody LED2 (czerwonej) */
					PORTD &= 191;
					
					/* je�li nie by�o b��du, ca�y bufor na pewno zosta� zapisany na karcie SD */
					if(!device_flags.sd_communication_error)
						/* ustawienie wska�nika w buforze na pocz�tek */
						buffer_index = 0;
				}
				else
/*				{*/
					/* ustawienie flagi b��du komunikacji z kart� SD */
					device_flags.sd_communication_error = 1;
					
// 					BlinkGreen(3, 200, 100);
// 					_delay_ms(300);
// 					BlinkRed(4, 200, 100);
// 				}
				
				/* pr�ba zamkni�cia pliku */
				if(f_close(&Fil) == FR_OK && !device_flags.sd_communication_error)
					/* aby nie pisa� 5 razy tego samego migania diodami, r�wnie� z tego case'a mo�e nast�pi� przej�cie do default'a,
					 * je�li wyst�pi b��d w jednej z 4 funkcji: f_open, f_seek, f_write lub f_close (taki zbiorczy else i default zarazem) */
					break;
			}
// 			else
// 			{
// 				BlinkGreen(3, 200, 100);
// 				_delay_ms(300);
// 				BlinkRed(2, 200, 100);
// 			}
		
		/* b��d, kt�ry wyst�pi� podczas komunikacji z kart� SD, zg�aszany jest u�ytkownikowi poprzez odpowiedni� sekwencj� migni�� diod
		 * (ten default jest zarazem obs�ug� b��d�w przy pr�bie otwarcia/utworzenia pliku "DoorLog.txt") */
		default:
			/* sekwencja migni�� diody czerwonej, sygnalizuj�ca u�ytkownikowi b��d komunikacji z kart� SD */
			BlinkRed(3, 200, 100);
			
			/* ustawienie flagi b��du komunikacji z kart� SD */
			device_flags.sd_communication_error = 1;
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
		
		SaveBuffer();
			
		/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi� b��d,
		 * urz�dzenie zasygnalizuje to jako zape�nienie bufora przy braku karty SD */
		if(device_flags.sd_communication_error)
		{
			device_flags.no_sd_card = device_flags.buffer_full = 1;
			device_flags.sd_communication_error = 0;
		}
		else
			/* wyczyszczenie flagi braku karty SD i flagi pe�nego bufora */
			device_flags.no_sd_card = device_flags.buffer_full = 0;
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
				/* rozpocz�cie operacji zmiany daty i czasu w RTC oraz zako�czenie ustawiania wszystkich sk�adowych i oczekiwanie na anulowanie/zatwierdzenie
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
						/* je�li w buforze brak miejsca na 2 rekordy, nale�y zapisa� jego zawarto�� na kart� SD */
						if(buffer_index > BUFFER_SIZE - 2)
						{
							SaveBuffer();
							
							/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi� b��d,
							 * urz�dzenie zasygnalizuje to w ten sam spos�b, co zape�nienie bufora przy braku karty SD */
							if(device_flags.sd_communication_error)
							{
								device_flags.buffer_full = 1;
								device_flags.sd_communication_error = 0;
							}
							else
								/* wyczyszczenie flagi pe�nego bufora przy braku karty SD */
								device_flags.buffer_full = 0;
						}
					
						if(!device_flags.buffer_full)
						{
							/* zapisanie do bufora rekordu o zdarzeniu */
							SaveEvent(4);
						
							/* zapisywanie w buforze stringowej reprezentacji nowych ustawie� daty i czasu dla RTC, w formacie YY-MM-DD HH:ii:SS */
							sprintf(buffer[buffer_index], "%02d-%02d-%02d %02d:%02d:%02d", set_rtc_values[Years], set_rtc_values[Century_months], set_rtc_values[Days],
								set_rtc_values[Hours], set_rtc_values[Minutes], set_rtc_values[VL_seconds]);
						
							/* przesuni�cie wska�nika w buforze o 1 pozycj� do przodu (normalnie robi to funkcja SaveEvent) */
							++buffer_index;
					
							/* zapisanie w RTC nowych ustawie� daty i godziny */
							RtcSetTime(set_rtc_values);
						
							/* czyszczenie flagi vl */
							device_flags.vl = 0;
						
							/* sygnalizacja wys�ania nowych ustawie� do RTC */
							BlinkGreen(1, 1500, 100);
						}
						else
							/* sygnalizacja anulowania zmiany ustawie� */
							BlinkRed(3, 100, 100);
					}
				
					/* zako�czenie ustawie� daty i godziny dla RTC */
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
			
			/* sygnalizacja inkrementacji bie��cej sk�adowej */
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
	
	/* Mog�o teraz wyst�pi� przerwanie o wy�szym priorytecie lub takowe mog�oby by� ju� obs�ugiwane gdy wyst�pi�o przerwanie z TIMER1.
	 * Przerwania o wy�szych priorytetach mog� (po�rednio lub bezpo�rednio) wywo�a� SaveBuffer, a wtedy poni�szy kod nie ma racji bytu.
	 * Dlatego najpierw sprawdzamy, czy licznik zawiera warto�� mniejsz� ni� startowa, co oznacza jego przepe�nienie. */
	if(TCNT1 < 36239)
	{
		/* zapisanie danych z bufora na kart� SD */
		SaveBuffer();
		
		/* je�li w trakcie operacji zapisu danych z bufora na kart� SD wyst�pi� b��d,
		 * urz�dzenie zasygnalizuje to w ten sam spos�b, co zape�nienie bufora przy braku karty SD */
		if(device_flags.sd_communication_error)
		{
			device_flags.buffer_full = 1;
			device_flags.sd_communication_error = 0;
			
			/* ustawienie w liczniku warto�ci startowej */
			TCNT1 = 36239;
		}
		else
		{
			/* wy��czenie Timera/Countera 1 */
			TCCR1B &= 250;
			
			/* wyczyszczenie flagi pe�nego bufora przy braku karty SD */
			device_flags.buffer_full = 0;
		}
	}
	
	/* umo�liwienie funkcji SaveBuffer w��czania przerwa� */
	device_flags.interrupts = 1;
}



/// Funkcja g��wna programu.
int main(void)
{
	/* zmienna iteracyjna u�ywana przy seriach 50-milisekundowych op�nie� w p�tli g��wnej programu
	 * auto - pr�ba wymuszenia alokacji tej zmiennej w rejestrze procesora */
	auto uint8_t delay = 0;
	
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

	/* domy�lne warto�ci w rejestrach DDRX i PORTX to 0, wpisuj� wi�c tylko 1 tam, gdzie to potrzebne */
	
	/* PB7(SCK) wyj�ciowy (zegar dla karty SD)			}
       PB6(MISO) wej�ciowy (dane odbierane z karty SD)	} inicjalizacja w
       PB5(MOSI) wyj�ciowy (dane wysy�ane do karty SD)	} plku sdmm.c
       PB4(SS) wyj�ciowy (slave select)					}
       PB2(INT2) wej�ciowy (przerwania zewn�trzne wywo�ywane przyciskami)
       PB1 wej�ciowy (przycisk)
       PB0 wej�ciowy (przycisk)*/
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
        /* flaga VL ustawiona => dioda zielona miga
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
		
		/* Je�li brak karty SD, zape�nienie bufora powoduje 2 razy cz�stsze miganie diody.
		 * Poniewa� funkcje _delay wy��czaj� przerwania, trzeba je w��cza� po zako�czeniu oczekiwania.
		 * Poniewa� odliczanie 10 sekund do ponownego sprawdzenia obecno�ci karty SD wi��e si� z 39 przerwaniami w ci�gu tych ok. 10 sekund,
		 * jedno przerwanie pojawia si� co ok. 0,25 s. Dlatego op�nienia dzielone s� na 50-milisekundowe odcinki, by co 50 ms mog�o zosta� obs�u�one przerwanie. */
		if(device_flags.buffer_full)
		{
			delay = 10;
			
			do
			{
				_delay_ms(50);
				sei();
			} while (--delay);
			
			PORTD ^= 64;
			
			delay = 10;
			
			do
			{
				_delay_ms(50);
				sei();
			} while (--delay);
		}
		else
		{
			delay = 20;
			
			do 
			{
				_delay_ms(50);
				sei();
			} while (--delay);
		}
    }
}
