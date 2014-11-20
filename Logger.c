/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gräser
 *
 *  Przycisk PB0 - rozpoczêcie operacji zmiany ustawieñ daty i czasu w RTC/przejœcie do kolejnego elementu daty lub czasu/zakoñczenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naciœniêcie zwiêkszenie o 1 bie¿¹cego elementu daty/czasu (wciœniêcie i przytrzymanie to pojedyncze naciœniêcie)
 *  TODO: opisaæ diody
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>



/* 0 oznacza tryb normalny, wartoœci od 1 do 7 to ustawianie kolejnych elementów daty i czasu w RTC, wartoœæ 8 to oczekiwanie na potwierdzenie b¹dŸ
anulowanie zmiany ustawieñ daty i czasu w RTC */
int set_rtc = 0;
/* wartoœci kolejnych rejestrów RTC, od VL_seconds [0] do Years [6], jakie maj¹ zostaæ ustawione po zatwierdzeniu operacji zmiany tych ustawieñ */
int set_rtc_values[7] = {0};
/* rozmiar bufora (liczba 21-bajtowych elementów do przechowywania rekordów o zdarzeniach) */
const int BUFFER_SIZE = 10;
/* bufor przechowuj¹cy do 10 rekordów informacyjnych o zarejestrowanych zdarzeniach */
char buffer[BUFFER_SIZE][21] = {'\0'};
/* przechowuje indeks elementu bufora, do którego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu */
int buffer_index = 0;


/* obs³uga przerwañ z kontaktronu (PD3) */
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
	
	/* TODO: rozpoczêcie (lub wymuszenie na pêtli g³ównej programu rozpoczêcia) odliczania czasu do zapisu danych z bufora na kartê SD */
}



/* obs³uga przerwañ z przycisków (PB2) */
ISR(INT2_vect)
{

}



int main(void)
{
	/************************************************************************/
	/*                     Inicjalizacja urz¹dzenia                         */
	/************************************************************************/
	
	/* w³¹czenie przerwañ zewnêtrznych INT1 i INT2 */
	GICR |= 1 << INT1;
	GICR |= 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj¹cym jest ustawiona domyœlnie */
	
	/* wy³¹czenie funkcji JTAG, aby móc u¿ywaæ portu C jako zwyk³ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
	/*************************** ustawianie pinów jako wejœciowe/wyjœciowe ****************************/
	
	/* domyœlne wartoœci w rejestrach DDRX i PORTX to 0, wpisujê wiêc tylko 1 tam, gdzie to potrzebne */
	/* TODO: scaliæ wszystkie ustawienia pinów I/O tak, aby by³y zbiorcze, po 1 na ka¿dy port */
	
	/* PB7(SCK) wyjœciowy (zegar dla karty SD)
       PB6(MISO) wejœciowy (dane odbierane z karty SD)
       PB5(MOSI) wyjœciowy (dane wysy³ane do karty SD)
       PB4(SS) wyjœciowy (slave select)
       PB2(INT2) wejœciowy (przerwania zewnêtrzne wywo³ywane przyciskami)
       PB1 wejœciowy (przycisk)
       PB0 wejœciowy (przycisk)*/
	DDRB = 1 << PB7 | 1 << PB5 | 1 << PB4;
	PORTB = 1 << PB6 | 1 << PB2 | 1 << PB1 | 1 << PB0;

	/* TODO: ustawienia dla PC1 i PC0 pod TWI (wew. pull-up'y pewnie w³¹czyæ, ale jako wej. czy wyj?) */

	/* PD7 i PD6 wyjœciowe (diody)
       PD3(INT1) wejœciowy (przerwania zewnêtrzne od kontaktronu) */
	DDRD = 1 << PD7 | 1 << PD6;
	PORTD = 1 << PD3;
	
	/* w³¹czenie przerwañ */
	sei();
	
	/************************************************************************/
	/*                       pêtla g³ówna programu                          */
	/************************************************************************/
    while(1)
    {
        
    }
}