/*
 * utils.h
 *
 * Utworzono: 2014-12-08 15:56:06
 * Autor: Adam Gr�ser
 */ 

#ifndef UTILS_H
#define UTILS_H

#include <util/delay.h>



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
} flags;



#pragma region ZmienneStaleMakra

/**
 * Warto�ci kolejnych rejestr�w RTC, od VL_seconds [0] do Years [5] (z pomini�ciem dni tygodnia), jakie maj� zosta� ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawie�.
 */
extern uint8_t set_rtc_values[6];

/// Flagi b��d�w i bie��cego stanu diod (u�ywane przy sekwencjach migni��).
extern flags device_flags = {0, 0, 0, 0, 0, 0};

#pragma endregion ZmienneStaleMakra



#pragma region FunkcjeInline

/// Ustawia warto�ci domy�lne w tablicy ustawie� daty i godziny dla RTC.
extern inline void RTCDefaultValues() __attribute__((always_inline));

/**
 * Miga zielon� diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia.
 * @param repeats Liczba migni��.
 * @param green_on Czas w milisekundach, przez jaki dioda ma si� �wieci�.
 * @param green_off Czas w milisekundach, przez jaki dioda ma si� nie �wieci�.
 */
extern inline void BlinkGreen(int repeats, int green_on, int green_off) __attribute__((always_inline));

/**
 * Miga czerwon� diod� wskazan� ilo�� razy, z podanymi czasami �wiecenia i nie�wiecenia.
 * @param repeats Liczba migni��.
 * @param red_on Czas w milisekundach, przez jaki dioda ma si� �wieci�.
 * @param red_off Czas w milisekundach, przez jaki dioda ma si� nie �wieci�.
 */
extern inline void BlinkRed(int repeats, int red_on, int red_off) __attribute__((always_inline));

#pragma endregion FunkcjeInline



#endif /* UTILS_H */
