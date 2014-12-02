#include "rtc.h"



void TwiStart(void)
{
	TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTA);
	while(!(TWCR & (1 << TWINT)));
}



void TwiStop(void)
{
	TWCR = (1 << TWINT)|(1 << TWEN)|(1 << TWSTO);
	while((TWCR & (1 << TWSTO)));
}



void TwiWrite(uint8_t data)
{
	TWDR = data;

	TWCR = (1 << TWINT) | (1 << TWEN);
	while(!(TWCR & (1 << TWINT)));
}



uint8_t TwiRead(uint8_t ack)
{
	TWCR = ack ? (((1 << TWINT) | (1 << TWEN) | (1 << TWEA))) : (((1 << TWINT) | (1 << TWEN)));
	
	while(!(TWCR & (1 << TWINT)));
	
	return TWDR;
}



void RtcGetTime (time *buf);
{
	/* nawi¹zanie po³¹czenia z RTC */
	twiStart();
	twiWrite(0xA2);				/* adres slave-receiver */
	twiWrite(0x02);				/* wystawienie adresu rejestru VL_seconds */
	twiStop();
	
	/* czytanie odpowiedzi z RTC */
	twiStart();
	twiWrite(0xA3);				/* adres slave-transmitter */
	*buf.seconds = twiRead(1);	/* aktywacja bitu potwierdzenia oznacza chêæ odczytu kolejnego bajta danych */
	*buf.minutes = twiRead(1);
	*buf.hours = twiRead(1);
	*buf.days = twiRead(1);
	twiRead(1);					/* dni tygodnia nie s¹ potrzebne - odczytujemy z koniecznoœci zachowania kolejnoœci */
	*buf.months = twiRead(1);
	*buf.years = twiRead(0);
	twiStop();
	
	/* TODO: sprawdzanie wartoœci bitu VL w VL_seconds i miganie jakoœ dziko diodami, jeœli to jest 1 */
	
	/* konwersja danych z kodu BCD, pominiêcie nieistotnych bitów */
	*buf.seconds = ((((*buf.seconds & 0x70) >> 4) * 10) + (*buf.seconds & 0x0F));
	*buf.minutes = ((((*buf.minutes & 0x70) >> 4) * 10) + (*buf.minutes & 0x0F));
	*buf.hours = ((((*buf.hours & 0x30) >> 4) * 10) + (*buf.hours & 0x0F));
	*buf.days = ((((*buf.days & 0x30) >> 4) * 10) + (*buf.days & 0x0F));
	*buf.months = ((((*buf.months & 0x10) >> 4) * 10) + (*buf.months & 0x0F));
	*buf.years = ((((*buf.years & 0xF0) >> 4) * 10) + (*buf.years & 0x0F));
}



void RtcSetTime (uint8_t *data)
{
	/* przekonwertowanie danych do kodu BCD */
	time temp;
	temp.seconds = ((data[VL_seconds] / 10) << 4) | (data[VL_seconds] % 10); /* 0 na najstarszym bicie -> wyczyszczenie bitu VL */
	temp.minutes = ((data[Minutes] / 10) << 4) | (data[Minutes] % 10);
	temp.hours = ((data[Hours] / 10) << 4) | (data[Hours] % 10);
	temp.days = ((data[Days] / 10) << 4) | (data[Days] % 10);
	temp.months = ((data[Century_months] / 10) << 4) | (data[Century_months] % 10);
	temp.years = ((data[Years] / 10) << 4) | (data[Years] % 10);
		
	/* przes³anie danych do RTC */
	twiStart();
	twiWrite(0xA2);			/* adres slave-receiver */
	twiWrite(0x02);			/* wystawienie adresu rejestru VL_seconds */
	twiWrite(temp.seconds);
	twiWrite(temp.minutes);
	twiWrite(temp.hours);
	twiWrite(temp.days);
	twiWrite(0);			/* dni tygodnia nie s¹ potrzebne - ustawiamy z koniecznoœci zachowania kolejnoœci */
	twiWrite(temp.months);  /* (bêdzie szybciej ni¿ przerwanie transmisji i rozpoczêcie nowej, z adresem rejestru Century_months) */
	twiWrite(temp.years);
	twiStop();
}
