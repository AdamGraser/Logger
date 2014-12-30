/*
 * utils.h
 *
 * Utworzono: 2014-12-08 15:56:06
 * Autor: Adam Gr�ser
 */ 

#ifndef UTILS_H
#define UTILS_H

/// Cz�stotliwo�� taktowania procesora. Zdefiniowane dla unikni�cia ostrze�enia kompilatora w util/delay.h
#define F_CPU 1000000UL

#include <util/delay.h>



/**
 * Pole bitowe przechowuj�ce flagi m.in. b��d�w.
 * @field led1 Bie��cy stan diody zielonej
 * @field led2 Bie��cy stan diody czerwonej
 * @field vl Warto�� bitu VL z rejestru VL_seconds w RTC (warto�� 1 informuje o utraceniu dok�adno�ci pomiaru czasu)
 * @field no_sd_card Flaga braku mo�liwego do zamontowania systemu plik�w
 * @field buffer_full Flaga zape�nienia bufora przy jednoczesnym braku karty SD lub flaga b��du zapisu danych na kart� SD
 * @field sd_communication_error Flaga b��du u�ywana wewn�trz funkcji {@link SaveBuffer}
 * @field interrupts Flaga determinuj�ca mo�liwo�� w��czenia przerwa�
 */
typedef struct
{
	uint8_t led1:1,
			led2:1,
			vl:1,
			no_sd_card:1,
			buffer_full:1,
			sd_communication_error:1,
			interrupts:1,
			reed_switch:1;
} flags;



/* sta�e u�ywane jako indeksy tablicy set_rtc_values, dla zwi�kszenia przejrzysto�ci kodu */
#define VL_seconds 0
#define Minutes 1
#define Hours 2
#define Days 3
#define Century_months 4
#define Years 5

/**
 * Warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [5] (z pomini�ciem dni tygodnia), jakie maj� zosta� ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawie�.
 */
extern uint8_t set_rtc_values[6];

/// Flagi b��d�w i bie��cego stanu diod (u�ywane przy sekwencjach migni��).
extern volatile flags device_flags;



/// Ustawia warto�ci domy�lne w tablicy ustawie� daty i godziny dla RTC.
#define RTCDefaultValues() \
{ \
	set_rtc_values[VL_seconds] = 0; \
	set_rtc_values[Minutes] = 0; \
	set_rtc_values[Hours] = 0; \
	set_rtc_values[Days] = 1; \
	set_rtc_values[Century_months] = 1; \
	set_rtc_values[Years] = 14; \
}

/**
 * Miga zielon� diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia.
 * @param repeats Liczba migni��.
 * @param green_on Czas w milisekundach, przez jaki dioda ma si� �wieci�.
 * @param green_off Czas w milisekundach, przez jaki dioda ma si� nie �wieci�.
 */
#define BlinkGreen(repeats, green_on, green_off) \
{ \
	/* zapisanie stanu, zgaszenie diody i odczekanie 'green_off' milisekund */ \
	if((PIND & (1 << PIND7))) \
	{ \
		device_flags.led1 = 1; \
		PORTD &= 127; \
	} \
	_delay_ms(green_off); \
\
	/* migni�cie diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia */ \
	/* zamiast tworzy� now� zmienn�, u�yto nieu�ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 128; \
		_delay_ms(green_on); \
\
		PORTD &= 127; \
		_delay_ms(green_off); \
	} \
\
	/* przywr�cenie stanu diody */ \
	PORTD |= device_flags.led1 << PD7; \
\
	/* wyczyszczenie flag z zapisanym stanem diod */ \
	device_flags.led1 = 0; \
}

/**
 * Miga czerwon� diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia.
 * @param repeats Liczba migni��.
 * @param red_on Czas w milisekundach, przez jaki dioda ma si� �wieci�.
 * @param red_off Czas w milisekundach, przez jaki dioda ma si� nie �wieci�.
 */
#define BlinkRed(repeats, red_on, red_off) \
{ \
	/* zapisanie stanu, zgaszenie diody i odczekanie 'red_off' milisekund */ \
	if((PIND & (1 << PIND6))) \
	{ \
		device_flags.led2 = 1; \
		PORTD &= 191; \
	} \
	_delay_ms(red_off); \
\
	/* migni�cie diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia */ \
	/* zamiast tworzy� now� zmienn�, u�yto nieu�ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 64; \
		_delay_ms(red_on); \
\
		PORTD &= 191; \
		_delay_ms(red_off); \
	} \
\
	/* przywr�cenie stanu diody */ \
	PORTD |= device_flags.led2 << PD6; \
\
	/* wyczyszczenie flag z zapisanym stanem diod */ \
	device_flags.led2 = 0; \
}



/**
 * Miga obiema diodami wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia.
 * @param repeats Liczba migni��.
 * @param on Czas w milisekundach, przez jaki diody maj� si� �wieci�.
 * @param off Czas w milisekundach, przez jaki diody maj� si� nie �wieci�.
 */
#define BlinkBoth(repeats, on, off) \
{ \
	/* zapisanie stanu, zgaszenie diod i odczekanie 'off' milisekund */ \
	if((PIND & (1 << PIND7))) \
	{ \
		device_flags.led1 = 1; \
		PORTD &= 127; \
	} \
	if((PIND & (1 << PIND6))) \
	{ \
		device_flags.led2 = 1; \
		PORTD &= 191; \
	} \
	_delay_ms(off); \
\
	/* migni�cie diodami wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia */ \
	/* zamiast tworzy� now� zmienn�, u�yto nieu�ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 192; \
		_delay_ms(on); \
\
		PORTD &= 63; \
		_delay_ms(off); \
	} \
\
	/* przywr�cenie stanu diod */ \
	PORTD |= device_flags.led1 << PD7 | device_flags.led2 << PD6; \
\
	/* wyczyszczenie flag z zapisanymi stanami diod */ \
	device_flags.led1 = device_flags.led2 = 0; \
}



/**
 * Wywo�uje funkcj� _delay_ms z biblioteki utils/delay.h 'd' razy, podaj�c jako argument warto�� 't'.<br>
 * Po ka�dym wywo�aniu op�nienia funkcja ta w��cza ponownie przerwania (_delay_ms wy��cza przerwania).
 * @param d Liczba powt�rze� op�nienia
 * @param t Czas op�nienia w milisekundach
 */
#define delay(d, t) \
{ \
	OCR0 = d; \
\
	do \
	{ \
		_delay_ms(t); \
		sei(); \
	} while (--OCR0); \
}



#endif /* UTILS_H */
