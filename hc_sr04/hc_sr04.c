#include <avr/io.h>
#include <stdlib.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <util/atomic.h>
#include "uart.h"

#define HARDWARE_TRIG	/* If using software trigger comment this line */

#ifndef HARDWARE_TRIG
#define SOFTWARE_TRIG
#endif 

#define BAUD 250000		/* configure baud rate for UART */

#define TRIGGER_PIN_SET_HIGH	PORTB|=(1<<PB0)
#define TRIGGER_PIN_SET_LOW		PORTB&=~(1<<PB0)

volatile enum {DATA_OLD, DATA_FRESH, DATA_BAD} FreshData;
volatile enum {STATE_FREE, STATE_BUSY} ConversionState;
volatile uint16_t length;
char buffer[4];

/* Function declaration */
void Init(void);

#ifdef SOFTWARE_TRIG
void Trig(void);
#endif /* SOFTWARE_TRIG */


int main(void){
	
	Init();	
	sei();	//enable interrupts
	
	while(1)
	{	
		if(FreshData == DATA_FRESH){	//send fresh (and correct) data only
			ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
				uart_puts(itoa(length/58,buffer,10));
			}
			uart_putc('\r');
			uart_putc('\n');
			FreshData=DATA_OLD;
		}
	}	
}

void Init(void){
	/* Trigger port configuration */
	DDRB |= 1<<PB0;		//trigger out
	TRIGGER_PIN_SET_LOW;
	/* Timer0 configuration */
	TCCR0 |= 1<<WGM01 | 1<<CS02 | 1<<CS00;	//CTC mode, prescaler=1024	
	TIMSK |= 1<<OCIE0;	//Output compare match interrupt enable
	OCR0 = 249;	//(FCPU=16000000)/(prescaler=1024)/(OCR0+1=250)=62,5 ticks per second (every 16ms)
	/* Timer1 configuration */
	TCCR1B |= 1<<ICES1;	//set rising edge
	TCCR1B |= 1<<CS11;	//prescaler=8
	TIMSK |=  1<<TICIE1;	//Timer1 Capture Interrupt Enable
	
#ifdef HARDWARE_TRIG
	TCCR1B |= 1<<WGM12;	//CTC mode
	OCR1A =	19; //19+1 cycles=10us (1 tick=0,5us)	
#endif
	/* UART configuration */
	uart_init((UART_BAUD_SELECT((BAUD),F_CPU)));
}

ISR(TIMER0_COMP_vect){	//62,5 times per second (every 16ms) ---> see Init() fucntion
	static uint8_t cnt=0;
	cnt++;
	if (cnt==4 && ConversionState==STATE_FREE){

#ifdef HARDWARE_TRIG	//trigger measurement by hardware - Timer1 CTC Mode
		TCCR1B |= 1<<WGM12;	//CTC mode
		TRIGGER_PIN_SET_HIGH;
		TCNT1 = 0; //reset Timer1
		TIFR |= 1<<OCF1A;	//clear any previous interrupt flag
		TIMSK |= 1<<OCIE1A;	//Output Compare A Match Interrupt Enable		
#endif

#ifdef SOFTWARE_TRIG
		Trig();	//trigger measurement
#endif

		cnt=0;
	} else if(cnt==4){	//after 4 ticks (64ms) still ConversionState=BUSY_STATE ---> it means timeout condition
			cnt=0;
			FreshData=DATA_BAD;
		}	
}

ISR(TIMER1_CAPT_vect){	//interrupt frequency = 2MHZ (F_CPU=16MHZ / prescaler=8), 1 tick every 0,5us
	if( (TCCR1B & (1<<ICES1)) )	//if rising edge
	{
		TCNT1 = 0;	//reset counter
		TCCR1B &= ~(1<<ICES1);	//set falling edge trigger
		ConversionState = STATE_BUSY;
	}else	//falling edge
	{
		if (FreshData != DATA_BAD){	//if timeout condition has not occurred
			length = ICR1/2;	//length reading	(1 tick=0,5us -> 200=100us  therefore divided by 2)		
			FreshData = DATA_FRESH;	//set marker, reseted after sending data in main function
		} else FreshData = DATA_OLD;	//next cycle will be handled correctly
		
		TCCR1B |= (1<<ICES1);	//set rising edge trigger
		ConversionState = STATE_FREE;
	}
}

#ifdef HARDWARE_TRIG

ISR(TIMER1_COMPA_vect){ /* interrupt occurs after 10us */
	TRIGGER_PIN_SET_LOW;		//end trigger signal
	TIMSK &= ~(1<<OCIE1A);	//Output Compare A Match Interrupt Disable
	TCCR1B &= ~(1<<WGM12);	//CTC mode disabled	
}
#endif /* HARDWARE_TRIG */

#ifdef SOFTWARE_TRIG

void Trig(void){
	TRIGGER_PIN_SET_HIGH;
	_delay_us(10);
	TRIGGER_PIN_SET_LOW;
}
#endif /* SOFTWARE_TRIG */
