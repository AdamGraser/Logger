#ifndef RTC_H
#define RTC_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>


#pragma region ZmienneStaleMakra

/* sta�e u�ywane jako indeksy tablicy set_rtc_values, dla zwi�kszenia przejrzysto�ci kodu */
#define VL_seconds 0
#define Minutes 1
#define Hours 2
#define Days 3
#define Century_months 4
#define Years 5

/**
 * Reprezentuje obiekt typu DateTime (przechowuje sk�adowe daty i czasu).
 * @field seconds sekundy
 * @field minutes minuty
 * @field hours godziny
 * @field days dni
 * @field months miesi�ce
 * @field years lata
 */
typedef struct {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t days;
	uint8_t months;
	uint8_t years;
} time;

#pragma endregion ZmienneStaleMakra


/// Sygnalizowanie rozpocz�cia transmisji danych na magistral� I2C za pomoc� TWI
void TwiStart(void);

/// Sygnalizowanie zako�czenia transmisji danych na magistral� I2C za pomoc� TWI
void TwiStop(void);

/**
 * Przes�anie 1 bajta danych na magistral� I2C za pomoc� TWI
 * @param data dane do przes�ania
 */
void TwiWrite(uint8_t data);

/**
 * Odczytanie 1 bajta danych z magistrali I2C za pomoc� TWI
 * @param ack 1 = w��czenie bitu potwierdzenia (po tej operacji nast�pi odczyt kolejnego bajta danych), 0 = wy��czenie bitu potwierdzenia (koniec czytania danych)
 * @return zawarto�� rejestru danych TWDR (dane odczytane z magistrali)
 */
uint8_t TwiRead(uint8_t ack);

/**
 * Pobranie bie��cej daty i czasu z zegara RTC PCF8563P
 * @param buf adres struktury, do kt�rej zapisane maj� zosta� data i czas pobrane z RTC
 */
void RtcGetTime (time *buf);

/**
 * Wys�anie do zegara RTC PCF8563P nowych ustawie� daty i czasu
 * @param data nowe ustawienia daty i czasu dla RTC
 */
void RtcSetTime (uint8_t *data);



#endif /* RTC_H */