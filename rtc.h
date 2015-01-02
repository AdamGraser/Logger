/*
 *  rtc.h
 *
 *  Utworzono: 2014-12-01 ??22:08:33
 *  Autor: Adam Gr�ser
 */

#ifndef RTC_H
#define RTC_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint-gcc.h>
#include "utils.h"



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



/// Sygnalizowanie rozpocz�cia transmisji danych na magistral� I2C za pomoc� TWI
void TwiStart(void);

/// Sygnalizowanie zako�czenia transmisji danych na magistral� I2C za pomoc� TWI
void TwiStop(void);

/**
 * Przes�anie 1 bajta danych na magistral� I2C za pomoc� TWI
 * @param data Dane do przes�ania
 */
void TwiWrite(uint8_t data);

/**
 * Odczytanie 1 bajta danych z magistrali I2C za pomoc� TWI
 * @param ack 1 = w��czenie bitu potwierdzenia (po tej operacji nast�pi odczyt kolejnego bajta danych), 0 = wy��czenie bitu potwierdzenia (koniec czytania danych)
 * @return Zawarto�� rejestru danych TWDR (dane odczytane z magistrali)
 */
uint8_t TwiRead(uint8_t ack);

/**
 * Pobranie bie��cej daty i czasu z zegara RTC PCF8563P
 * @param buf Adres struktury, do kt�rej zapisane maj� zosta� data i czas pobrane z RTC
 */
void RtcGetTime (time *buf);

/**
 * Wys�anie do zegara RTC PCF8563P nowych ustawie� daty i czasu
 * @param data Nowe ustawienia daty i czasu dla RTC
 */
void RtcSetTime (uint8_t *data);



#endif /* RTC_H */
