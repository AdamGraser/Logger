/*
 * utils.c
 *
 * Utworzono: 2014-12-08 16:01:17
 * Autor: Adam Gräser
 */ 

#include "utils.h"



void RTCDefaultValues()
{
	set_rtc_values[VL_seconds] = 0;
	set_rtc_values[Minutes] = 0;
	set_rtc_values[Hours] = 0;
	set_rtc_values[Days] = 1;
	set_rtc_values[Century_months] = 1;
	set_rtc_values[Years] = 14;
}



void BlinkGreen(int repeats, int green_on, int green_off)
{
	/* zapisanie stanu, zgaszenie diody i odczekanie 'green_off' milisekund */
	if((PIND & (1 << PIND7)))
	{
		device_flags.led1 = 1;
		PORTD &= 127;
	}
	_delay_ms(green_off);
	
	/* migniêcie diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia */
	do
	{
		PORTD |= 128;
		_delay_ms(green_on);
		
		PORTD &= 127;
		_delay_ms(green_off);
	}
	while(--repeats);
	
	/* przywrócenie stanu diody */
	PORTD |= device_flags.led1 << PD7;
	
	/* wyczyszczenie flag z zapisanym stanem diod */
	device_flags.led1 = 0;
}



void BlinkRed(int repeats, int red_on, int red_off)
{
	/* zapisanie stanu, zgaszenie diody i odczekanie 'red_off' milisekund */
	if((PIND & (1 << PIND6)))
	{
		device_flags.led2 = 1;
		PORTD &= 191;
	}
	_delay_ms(red_off);
	
	/* migniêcie diod¹ wskazan¹ iloœæ razy, z podanymi czasami œwiecenia i nieœwiecenia */
	do
	{
		PORTD |= 64;
		_delay_ms(red_on);
		
		PORTD &= 191;
		_delay_ms(red_off);
	}
	while(--repeats);
	
	/* przywrócenie stanu diody */
	PORTD |= device_flags.led2 << PD6;
	
	/* wyczyszczenie flag z zapisanym stanem diod */
	device_flags.led2 = 0;
}
