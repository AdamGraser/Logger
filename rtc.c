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



time RtcGetTime (void)
{
	time temp;
	
	/* nawi¹zanie po³¹czenia z RTC */
	twiStart();
	twiWrite(0xA2);				/* adres slave-receiver */
	twiWrite(0x02);				/* wystawienie adresu rejestru VL_seconds */
	twiStop();
	
	/* czytanie odpowiedzi z RTC */
	twiStart();
	twiWrite(0xA3);				/* adres slave-transmitter */
	temp.seconds = twiRead(1);	/* aktywacja bitu potwierdzenia oznacza chêæ odczytu kolejnego bajta danych */
	temp.minutes = twiRead(1);
	temp.hours = twiRead(1);
	temp.days = twiRead(1);
	twiRead(1);					/* dni tygodnia nie s¹ potrzebne - odczytujemy z koniecznoœci zachowania kolejnoœci */
	temp.months = twiRead(1);
	temp.years = twiRead(0);
	twiStop();
	
	/* TODO: sprawdzanie wartoœci bitu VL w VL_seconds i miganie jakoœ dziko diodami, jeœli to jest 1 */
	
	/* konwersja danych z kodu BCD, pominiêcie nieistotnych bitów */
	temp.seconds = ((((temp.seconds & 0x70) >> 4) * 10) + (temp.seconds & 0x0F));
	temp.minutes = ((((temp.minutes & 0x70) >> 4) * 10) + (temp.minutes & 0x0F));
	temp.hours = ((((temp.hours & 0x30) >> 4) * 10) + (temp.hours & 0x0F));
	temp.days = ((((temp.days & 0x30) >> 4) * 10) + (temp.days & 0x0F));
	temp.months = ((((temp.months & 0x10) >> 4) * 10) + (temp.months & 0x0F));
	temp.years = ((((temp.years & 0xF0) >> 4) * 10) + (temp.years & 0x0F));
	
	return temp;	
}



void RtcSetTime (time data)
{
	/* przekonwertowanie danych do kodu BCD */
	time temp = data;
	temp.seconds = ((data.seconds / 10) << 4) | (data.seconds % 10); /* 0 na najstarszym bicie -> wyczyszczenie bitu VL */
	temp.minutes = ((temp.minutes / 10) << 4) | (temp.minutes % 10);
	temp.hours = ((temp.hours / 10) << 4) | (temp.hours % 10);
	temp.days = ((temp.days / 10) << 4) | (temp.days % 10);
	temp.months = ((temp.months / 10) << 4) | (temp.months % 10);
	temp.years = ((temp.years / 10) << 4) | (temp.years % 10);
		
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
