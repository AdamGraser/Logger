/*
 *  rtc.h
 *
 *  Utworzono: 2014-12-01 ??22:08:33
 *  Autor: Adam Gräser
 */

#ifndef RTC_H
#define RTC_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>
#include "utils.h"



/**
 * Reprezentuje obiekt typu DateTime (przechowuje sk³adowe daty i czasu).
 * @field seconds sekundy
 * @field minutes minuty
 * @field hours godziny
 * @field days dni
 * @field months miesi¹ce
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



/// Sygnalizowanie rozpoczêcia transmisji danych na magistralê I2C za pomoc¹ TWI
void TwiStart(void);

/// Sygnalizowanie zakoñczenia transmisji danych na magistralê I2C za pomoc¹ TWI
void TwiStop(void);

/**
 * Przes³anie 1 bajta danych na magistralê I2C za pomoc¹ TWI
 * @param data Dane do przes³ania
 */
void TwiWrite(uint8_t data);

/**
 * Odczytanie 1 bajta danych z magistrali I2C za pomoc¹ TWI
 * @param ack 1 = w³¹czenie bitu potwierdzenia (po tej operacji nast¹pi odczyt kolejnego bajta danych), 0 = wy³¹czenie bitu potwierdzenia (koniec czytania danych)
 * @return Zawartoœæ rejestru danych TWDR (dane odczytane z magistrali)
 */
uint8_t TwiRead(uint8_t ack);

/**
 * Pobranie bie¿¹cej daty i czasu z zegara RTC PCF8563P
 * @param buf Adres struktury, do której zapisane maj¹ zostaæ data i czas pobrane z RTC
 */
void RtcGetTime (time *buf);

/**
 * Wys³anie do zegara RTC PCF8563P nowych ustawieñ daty i czasu
 * @param data Nowe ustawienia daty i czasu dla RTC
 */
void RtcSetTime (uint8_t *data);



#endif /* RTC_H */
