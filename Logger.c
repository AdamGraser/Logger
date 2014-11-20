/*
 *  Logger.c
 *
 *  Utworzono: 2014-11-20 18:53:19
 *  Autor: Adam Gr�ser
 *
 *  Przycisk PB0 - rozpocz�cie operacji zmiany ustawie� daty i czasu w RTC/przej�cie do kolejnego elementu daty lub czasu/zako�czenie operacji zmiany...
 *  Przycisk PB1 - pojedyncze naci�ni�cie zwi�kszenie o 1 bie��cego elementu daty/czasu (wci�ni�cie i przytrzymanie to pojedyncze naci�ni�cie)
 *  TODO: opisa� diody
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>



/* 0 oznacza tryb normalny, warto�ci od 1 do 7 to ustawianie kolejnych element�w daty i czasu w RTC, warto�� 8 to oczekiwanie na potwierdzenie b�d�
anulowanie zmiany ustawie� daty i czasu w RTC */
int set_rtc = 0;
/* warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [6], jakie maj� zosta� ustawione po zatwierdzeniu operacji zmiany tych ustawie� */
int set_rtc_values[7] = {0};
/* rozmiar bufora (liczba 21-bajtowych element�w do przechowywania rekord�w o zdarzeniach) */
const int BUFFER_SIZE = 10;
/* bufor przechowuj�cy do 10 rekord�w informacyjnych o zarejestrowanych zdarzeniach */
char buffer[BUFFER_SIZE][21] = {'\0'};
/* przechowuje indeks elementu bufora, do kt�rego zapisany zostanie najnowszy rekord o zarejestrowanym zdarzeniu */
int buffer_index = 0;


/* obs�uga przerwa� z kontaktronu (PD3) */
ISR(INT1_vect)
{
	/* TODO: odczyt czasu i daty z RTC, wstawienie go do rekordu */
	
	buffer[buffer_index] = "YYYY-MM-DD hh:mm:ss ";
	
	/* PD3 == 1 -> drzwi otwarte */
	if(PIND & (1 << PD3))
		buffer[buffer_index][20] = 'o';
	/* PD3 == 0 -> drzwi zamkni�te */
	else
		buffer[buffer_index][20] = 'c';
	
	/* TODO: rozpocz�cie (lub wymuszenie na p�tli g��wnej programu rozpocz�cia) odliczania czasu do zapisu danych z bufora na kart� SD */
}



/* obs�uga przerwa� z przycisk�w (PB2) */
ISR(INT2_vect)
{

}



int main(void)
{
	/************************************************************************/
	/*                     Inicjalizacja urz�dzenia                         */
	/************************************************************************/
	
	/* w��czenie przerwa� zewn�trznych INT1 i INT2 */
	GICR |= 1 << INT1;
	GICR |= 1 << INT2;
	/* ustawienie generacji przerwania INT1 przy dowolnej zmianie poziomu logicznego */
	MCUCR |= 0 << ISC11 | 1 << ISC10;
	/* generacja przerwania INT2 przy zboczu opadaj�cym jest ustawiona domy�lnie */
	
	/* wy��czenie funkcji JTAG, aby m�c u�ywa� portu C jako zwyk�ego portu I/O */
	MCUCSR |= (1 << JTD);
	MCUCSR |= (1 << JTD);
	
	/*************************** ustawianie pin�w jako wej�ciowe/wyj�ciowe ****************************/
	
	/* domy�lne warto�ci w rejestrach DDRX i PORTX to 0, wpisuj� wi�c tylko 1 tam, gdzie to potrzebne */
	/* TODO: scali� wszystkie ustawienia pin�w I/O tak, aby by�y zbiorcze, po 1 na ka�dy port */
	
	/* PB7(SCK) wyj�ciowy (zegar dla karty SD)
       PB6(MISO) wej�ciowy (dane odbierane z karty SD)
       PB5(MOSI) wyj�ciowy (dane wysy�ane do karty SD)
       PB4(SS) wyj�ciowy (slave select)
       PB2(INT2) wej�ciowy (przerwania zewn�trzne wywo�ywane przyciskami)
       PB1 wej�ciowy (przycisk)
       PB0 wej�ciowy (przycisk)*/
	DDRB = 1 << PB7 | 1 << PB5 | 1 << PB4;
	PORTB = 1 << PB6 | 1 << PB2 | 1 << PB1 | 1 << PB0;

	/* TODO: ustawienia dla PC1 i PC0 pod TWI (wew. pull-up'y pewnie w��czy�, ale jako wej. czy wyj?) */

	/* PD7 i PD6 wyj�ciowe (diody)
       PD3(INT1) wej�ciowy (przerwania zewn�trzne od kontaktronu) */
	DDRD = 1 << PD7 | 1 << PD6;
	PORTD = 1 << PD3;
	
	/* w��czenie przerwa� */
	sei();
	
	/************************************************************************/
	/*                       p�tla g��wna programu                          */
	/************************************************************************/
    while(1)
    {
        
    }
}