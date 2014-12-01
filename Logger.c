/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gräser
 *
 *  Przycisk PB0 - rozpoczêcie operacji zmiany ustawieñ daty i czasu w RTC/przejœcie do kolejnego elementu daty lub czasu/zakoñczenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naciœniêcie zwiêkszenie o 1 bie¿¹cego elementu daty/czasu (wciœniêcie i przytrzymanie to pojedyncze naciœniêcie)
 *                 wyjœcie poza zakres danej sk³adowej daty/czasu powoduje jej wyzerowanie
 *  TODO: opisaæ diody
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>



/* zmienne globalne, sta³e, makra */
#pragma region ZmienneStaleMakra

/* sta³e u¿ywane jako indeksy tablicy set_rtc_values, dla zwiêkszenia przejrzystoœci */
#define VL_seconds 0
#define Minutes 1
#define Hours 2
#define Days 3
#define Century_months 4
#define Years 5

/* -1 oznacza tryb normalny, wartoœci od 0 do 5 to ustawianie kolejnych elementów daty i czasu w RTC, wartoœæ 6 to oczekiwanie na potwierdzenie
   b¹dŸ anulowanie zmiany ustawieñ daty i czasu w RTC */
int set_rtc = -1;
/* wartoœci kolejnych rejestrów RTC, od VL_seconds [0] do Years [5] (z pominiêciem dni tygodnia), jakie maj¹ zostaæ ustawione po zatwierdzeniu
   operacji zmiany tych ustawieñ */
unsigned int set_rtc_values[6];
/* determinuje czy zmiana ustawieñ daty i godziny zosta³a anulowana czy nie */
bool set_rtc_cancelled = false;
/* rozmiar bufora (liczba 21-bajtowych elementów do przechowywania rekordów o zdarzeniach) */
const int BUFFER_SIZE = 10;
/* bufor przechowuj¹cy do 10 rekordów informacyjnych o zarejestrowanych zdarzeniach */
char buffer[BUFFER_SIZE][22];
/* przechowuje indeks elementu bufora, do którego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu */
int buffer_index = 0;

/* makro ustawiaj¹ce wartoœci domyœlne w tablicy ustawieñ daty i godziny dla RTC */
#define RTCDefaultValues()
{
	set_rtc_values[VL_seconds] = 0;
	set_rtc_values[Minutes] = 0;
	set_rtc_values[Hours] = 0;
	set_rtc_values[Days] = 1;
	set_rtc_values[Century_months] = 1;
	set_rtc_values[Years] = 2014;
}

#pragma endregion ZmienneStaleMakra



/****************************************************************************************/
/* Konwertuje elementy tablicy z nowymi ustawieniami daty i czasu RTC do tablic znaków. */
/* Wynikowy napis, bêd¹cy tekstow¹ reprezentacj¹ nowej daty i czasu, zapisuje we        */
/* wskazywanym przez 'buffer_index' elemencie bufora.                                   */
/****************************************************************************************/
void NewDateTimeToString()
{
	char temp[5] = {'\0'};
	
	/* zerowanie docelowej tablicy znaków na potrzeby funkcji strcpy */
	memset((void*)buffer[buffer_index], 0, 22);
	
	/* ROK */
	    /* konwersja */
	sprintf(temp, "%d", set_rtc_values[Years]);
	    /* skopiowanie sk³adowej do bufora */
	strcpy(buffer[buffer_index], temp);
	    /* dopisanie separatora */
	buffer[buffer_index][strlen(buffer[buffer_index])] = '-';
	    /* wyzerowanie tablicy tymczasowej */
	temp[0] = temp[1] = temp[2] = temp[3] = '\0';
	
	/* MIESI¥C */
	sprintf(temp, "%d", set_rtc_values[Century_months]);
	strcpy(buffer[buffer_index], temp);
	buffer[buffer_index][strlen(buffer[buffer_index])] = '-';
	temp[0] = temp[1] = '\0';
	
	/* DZIEÑ */
	sprintf(temp, "%d", set_rtc_values[Days]);
	strcpy(buffer[buffer_index], temp);
	buffer[buffer_index][strlen(buffer[buffer_index])] = ' ';
	temp[0] = temp[1] = '\0';
	
	/* GODZINA */
	sprintf(temp, "%d", set_rtc_values[Hours]);
	strcpy(buffer[buffer_index], temp);
	buffer[buffer_index][strlen(buffer[buffer_index])] = ':';
	temp[0] = temp[1] = '\0';
	
	/* MINUTA */
	sprintf(temp, "%d", set_rtc_values[Minutes]);
	strcpy(buffer[buffer_index], temp);
	buffer[buffer_index][strlen(buffer[buffer_index])] = ':';
	temp[0] = temp[1] = '\0';
	
	/* SEKUNDA */
	sprintf(temp, "%d", set_rtc_values[VL_seconds]);
	strcpy(buffer[buffer_index], temp);
}



/************************************************************************/
/***************** obs³uga przerwañ z kontaktronu (PD3) *****************/
/************************************************************************/
ISR(INT1_vect)
{
	/* TODO: odczyt czasu i daty z RTC, wstawienie go do rekordu */
	
	buffer[buffer_index] = "YYYY-MM-DD hh:mm:ss ";
	
	/* PD3 == 1 -> drzwi otwarte */
	if(PIND & (1 << PD3))
		buffer[buffer_index][20] = 'o';
	/* PD3 == 0 -> drzwi zamkniête */
	else
		buffer[buffer_index][20] = 'c';
	
	/* TODO: rozpoczêcie/zrestartowanie (lub wymuszenie na pêtli g³ównej programu rozpoczêcia) odliczania czasu do zapisu danych z bufora na kartê SD */
}



/*************************************************************************/
/****************** obs³uga przerwañ z przycisków (PB2) ******************/
/*************************************************************************/
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
				}
				/* w przeciwnym razie wysy³amy nowe ustawienia do RTC */
				else
				{
					/* TODO: odczyt czasu i daty z RTC, wstawienie go do rekordu */
					
					if(buffer_index > BUFFER_SIZE - 2)
					{
						/* TODO: zapisanie danych z bufora na kartê SD razem z tymi 2 najnowszymi rekordami, które by siê do bufora nie zmieœci³y */
						
						buffer_index = 0;
					}
					else
					{
						buffer[buffer_index] = "YYYY-MM-DD hh:mm:ss d";
						++buffer_index;
						NewDateTimeToString();
						++buffer_index;
					
						/* TODO: rozpoczêcie/zrestartowanie (lub wymuszenie na pêtli g³ównej programu rozpoczêcia) odliczania czasu do zapisu danych z bufora na kartê SD */
					}
					
					/* TODO: wysy³anie nowych ustawieñ daty i godziny do RTC */
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
			set_rtc_cancelled = true;
		}
		else
		{
			/* pojedyncze wciœniêcie przycisku to zwiêkszenie bie¿¹cej sk³adowej o 1 */
			++set_rtc_values[set_rtc];
			
			/* kontrola zakresu wartoœci dla bie¿¹cej sk³adowej */
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
						if((set_rtc_values[Years] % 4 == 0 && set_rtc_values[Years] % 100 != 0) || set_rtc_values[Years] % 400 == 0)
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
			}
		}
	}
}



int main(void)
{
	/************************************************************************/
	/*                     Inicjalizacja urz¹dzenia                         */
	/************************************************************************/
	
	/* wyzerowanie bufora */
	memset((void*)buffer, 0, BUFFER_SIZE * 22);
	/* ustawienie wartoœci domyœlnych w tablicy ustawieñ daty i godziny dla RTC */
	RTCDefaultValues();
	
	/* w³¹czenie przerwañ zewnêtrznych INT1 i INT2 */
	GICR |= 1 << INT1;
	GICR |= 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj¹cym jest ustawiona domyœlnie */
	
	/* wy³¹czenie funkcji JTAG, aby móc u¿ywaæ portu C jako zwyk³ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
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
	   w³¹czam wystêpowanie bitu potwierdzenia (ustawienie bitu TWEA - TWI Enable Acknowledge bit)
	   w³¹czam TWI mo¿liwoœæ wywo³ywania przerwañ (ustawienie bitu TWIE - TWI Interrupt Enable)
	   TWCR - TWI Control Register */
	TWCR |= 1 << TWEA | 1 << TWEN | 1 << TWIE;
	
	/* ustawienie czêstotliwoœci dla TWI:
	   SCL frequency = CPU Clock frequency / (16 + 2(TWBR) * 4^TWPS)
	   dla TWBR = 0 i TWPS = 00 (wartoœci domyœlne) powy¿sze równanie da dzielnik równy 16
	   przy wewnêtrznym zegarze Atmegi taktuj¹cym z czêst. 1 MHz, otrzymam dla TWI czêst. 62,5 KHz
	   TWBR - TWI Bit rate Register
	   TWSR - TWI Status Register:
	       TWSR1:0 -> TWPS1, TWPS0 - TWI PreScaler bits */
	
#pragma endregion UstawieniaTWI
	
	/* w³¹czenie przerwañ */
	sei();
	
	/************************************************************************/
	/*                       pêtla g³ówna programu                          */
	/************************************************************************/
    for(;;)
    {
        
    }
}