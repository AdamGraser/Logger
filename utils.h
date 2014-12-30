/*
 * utils.h
 *
 * Utworzono: 2014-12-08 15:56:06
 * Autor: Adam Gräser
 */ 

#ifndef UTILS_H
#define UTILS_H

/// Czêstotliwoœæ taktowania procesora. Zdefiniowane dla unikniêcia ostrze¿enia kompilatora w util/delay.h
#define F_CPU 1000000UL

#include <util/delay.h>



/**
 * Pole bitowe przechowuj¹ce flagi m.in. b³êdów.
 * @field led1 Bie¿¹cy stan diody zielonej
 * @field led2 Bie¿¹cy stan diody czerwonej
 * @field vl Wartoœæ bitu VL z rejestru VL_seconds w RTC (wartoœæ 1 informuje o utraceniu dok³adnoœci pomiaru czasu)
 * @field no_sd_card Flaga braku mo¿liwego do zamontowania systemu plików
 * @field buffer_full Flaga zape³nienia bufora przy jednoczesnym braku karty SD lub flaga b³êdu zapisu danych na kartê SD
 * @field sd_communication_error Flaga b³êdu u¿ywana wewn¹trz funkcji {@link SaveBuffer}
 * @field interrupts Flaga determinuj¹ca mo¿liwoœæ w³¹czenia przerwañ
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



/* sta³e u¿ywane jako indeksy tablicy set_rtc_values, dla zwiêkszenia przejrzystoœci kodu */
#define VL_seconds 0
#define Minutes 1
#define Hours 2
#define Days 3
#define Century_months 4
#define Years 5

/**
 * Wartoœci kolejnych rejestrów RTC, od VL_seconds [0] do Years [5] (z pominiêciem dni tygodnia), jakie maj¹ zostaæ ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawieñ.
 */
extern uint8_t set_rtc_values[6];

/// Flagi b³êdów i bie¿¹cego stanu diod (u¿ywane przy sekwencjach migniêæ).
extern volatile flags device_flags;



/// Ustawia wartoœci domyœlne w tablicy ustawieñ daty i godziny dla RTC.
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
 * Miga zielon¹ diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia.
 * @param repeats Liczba migniêæ.
 * @param green_on Czas w milisekundach, przez jaki dioda ma siê œwieciæ.
 * @param green_off Czas w milisekundach, przez jaki dioda ma siê nie œwieciæ.
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
	/* migniêcie diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia */ \
	/* zamiast tworzyæ now¹ zmienn¹, u¿yto nieu¿ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 128; \
		_delay_ms(green_on); \
\
		PORTD &= 127; \
		_delay_ms(green_off); \
	} \
\
	/* przywrócenie stanu diody */ \
	PORTD |= device_flags.led1 << PD7; \
\
	/* wyczyszczenie flag z zapisanym stanem diod */ \
	device_flags.led1 = 0; \
}

/**
 * Miga czerwon¹ diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia.
 * @param repeats Liczba migniêæ.
 * @param red_on Czas w milisekundach, przez jaki dioda ma siê œwieciæ.
 * @param red_off Czas w milisekundach, przez jaki dioda ma siê nie œwieciæ.
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
	/* migniêcie diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia */ \
	/* zamiast tworzyæ now¹ zmienn¹, u¿yto nieu¿ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 64; \
		_delay_ms(red_on); \
\
		PORTD &= 191; \
		_delay_ms(red_off); \
	} \
\
	/* przywrócenie stanu diody */ \
	PORTD |= device_flags.led2 << PD6; \
\
	/* wyczyszczenie flag z zapisanym stanem diod */ \
	device_flags.led2 = 0; \
}



/**
 * Miga obiema diodami wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia.
 * @param repeats Liczba migniêæ.
 * @param on Czas w milisekundach, przez jaki diody maj¹ siê œwieciæ.
 * @param off Czas w milisekundach, przez jaki diody maj¹ siê nie œwieciæ.
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
	/* migniêcie diodami wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia */ \
	/* zamiast tworzyæ now¹ zmienn¹, u¿yto nieu¿ywanego rejestru Output Compare Register Timer/Counter0 */ \
	for(OCR0 = 0; OCR0 < repeats; ++OCR0) \
	{ \
		PORTD |= 192; \
		_delay_ms(on); \
\
		PORTD &= 63; \
		_delay_ms(off); \
	} \
\
	/* przywrócenie stanu diod */ \
	PORTD |= device_flags.led1 << PD7 | device_flags.led2 << PD6; \
\
	/* wyczyszczenie flag z zapisanymi stanami diod */ \
	device_flags.led1 = device_flags.led2 = 0; \
}



/**
 * Wywo³uje funkcjê _delay_ms z biblioteki utils/delay.h 'd' razy, podaj¹c jako argument wartoœæ 't'.<br>
 * Po ka¿dym wywo³aniu opóŸnienia funkcja ta w³¹cza ponownie przerwania (_delay_ms wy³¹cza przerwania).
 * @param d Liczba powtórzeñ opóŸnienia
 * @param t Czas opóŸnienia w milisekundach
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
