/*
 * utils.h
 *
 * Utworzono: 2014-12-08 15:56:06
 * Autor: Adam Gräser
 */ 

#ifndef UTILS_H
#define UTILS_H

#include <util/delay.h>



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
} flags;



#pragma region ZmienneStaleMakra

/**
 * Wartoœci kolejnych rejestrów RTC, od VL_seconds [0] do Years [5] (z pominiêciem dni tygodnia), jakie maj¹ zostaæ ustawione w RTC po zatwierdzeniu
 * operacji zmiany tych ustawieñ.
 */
extern uint8_t set_rtc_values[6];

/// Flagi b³êdów i bie¿¹cego stanu diod (u¿ywane przy sekwencjach migniêæ).
extern flags device_flags = {0, 0, 0, 0, 0, 0};

#pragma endregion ZmienneStaleMakra



#pragma region FunkcjeInline

/// Ustawia wartoœci domyœlne w tablicy ustawieñ daty i godziny dla RTC.
extern inline void RTCDefaultValues() __attribute__((always_inline));

/**
 * Miga zielon¹ diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia.
 * @param repeats Liczba migniêæ.
 * @param green_on Czas w milisekundach, przez jaki dioda ma siê œwieciæ.
 * @param green_off Czas w milisekundach, przez jaki dioda ma siê nie œwieciæ.
 */
extern inline void BlinkGreen(int repeats, int green_on, int green_off) __attribute__((always_inline));

/**
 * Miga czerwon¹ diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia.
 * @param repeats Liczba migniêæ.
 * @param red_on Czas w milisekundach, przez jaki dioda ma siê œwieciæ.
 * @param red_off Czas w milisekundach, przez jaki dioda ma siê nie œwieciæ.
 */
extern inline void BlinkRed(int repeats, int red_on, int red_off) __attribute__((always_inline));

#pragma endregion FunkcjeInline



#endif /* UTILS_H */
