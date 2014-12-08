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



void RtcGetTime (time *buf)
{
	/* nawi¹zanie po³¹czenia z RTC */
	TwiStart();
	TwiWrite(0xA2);				/* adres slave-receiver */
	TwiWrite(0x02);				/* wystawienie adresu rejestru VL_seconds */
	TwiStop();
	
	/* czytanie odpowiedzi z RTC */
	TwiStart();
	TwiWrite(0xA3);				/* adres slave-transmitter */
	buf->seconds = TwiRead(1);	/* aktywacja bitu potwierdzenia oznacza chêæ odczytu kolejnego bajta danych */
	buf->minutes = TwiRead(1);
	buf->hours = TwiRead(1);
	buf->days = TwiRead(1);
	TwiRead(1);					/* dni tygodnia nie s¹ potrzebne - odczytujemy z koniecznoœci zachowania kolejnoœci */
	buf->months = TwiRead(1);
	buf->years = TwiRead(0);
	TwiStop();
	
	/* jeœli najstarszy bit rejestru VL_seconds (flaga VL) ma wartoœæ 1, to oznacza utratê dok³adnoœci pomiaru czasu,
	 * nale¿y wiêc ustawiæ odpowiedni¹ flagê globaln¹ programu */
	if(buf->seconds & 128)
		device_flags.vl = 1;
	
	/* konwersja danych z kodu BCD, pominiêcie nieistotnych bitów */
	buf->seconds = ((((buf->seconds & 0x70) >> 4) * 10) + (buf->seconds & 0x0F));
	buf->minutes = ((((buf->minutes & 0x70) >> 4) * 10) + (buf->minutes & 0x0F));
	buf->hours = ((((buf->hours & 0x30) >> 4) * 10) + (buf->hours & 0x0F));
	buf->days = ((((buf->days & 0x30) >> 4) * 10) + (buf->days & 0x0F));
	buf->months = ((((buf->months & 0x10) >> 4) * 10) + (buf->months & 0x0F));
	buf->years = ((((buf->years & 0xF0) >> 4) * 10) + (buf->years & 0x0F));
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
	TwiStart();
	TwiWrite(0xA2);			/* adres slave-receiver */
	TwiWrite(0x02);			/* wystawienie adresu rejestru VL_seconds */
	TwiWrite(temp.seconds);
	TwiWrite(temp.minutes);
	TwiWrite(temp.hours);
	TwiWrite(temp.days);
	TwiWrite(0);			/* dni tygodnia nie s¹ potrzebne - ustawiamy z koniecznoœci zachowania kolejnoœci */
	TwiWrite(temp.months);  /* (bêdzie szybciej ni¿ przerwanie transmisji i rozpoczêcie nowej, z adresem rejestru Century_months) */
	TwiWrite(temp.years);
	TwiStop();
}
