/*
 * Testcenter.c
 *
 *  Created on: Dec 8, 2016
 *      Author: dreifuerstrm
 *      Project full of snippets of code for testing, as well as the in-progress main program for
 *      the sumobot competition. More information can be found in the attached notebook.
 *      Parts being used are:
 *      perforated sheet metal
 *      Arduino uno
 *      screwshield prototyping board   https://learn.adafruit.com/adafruit-proto-screw-shield/make-it
 *      LD293 Motor Driver  IC https://faculty-web.msoe.edu/johnsontimoj/ee2931W16/files2931/L293D_Motor_Driver.pdf
 *      7404 Not gate
 *      IR receiver   https://faculty-web.msoe.edu/johnsontimoj/ee2931W16/files2931/PNA4601M.pdf
 *      IR LED   https://faculty-web.msoe.edu/johnsontimoj/ee2931W16/files2931/IRInfoSheet.pdf
 *      QTR-1A    https://faculty-web.msoe.edu/johnsontimoj/ee2920F16/files2920/QTR-1A.pdf
 *      LEDs
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <MSOE_I2C/lcd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

float irtime;
int irL, irR, lastL, lastR; //variables to store the line sensor reading
int irpos = 0; //Tracks the amount of time for the IR signal
long rotator = 0; //Tracks the amount of time since the last straight phase of searching
typedef enum state{searching, approaching, pushing} States; //state machine
States mystate;

void fivekHz(){
	TCCR1A = 0B10100010; //clear on compare for a and b, top is value in input capture
	TCCR1B = 0X19; //no prescaler, no input capture
	TCNT1 = 0; //reset clock
	ICR1 = 3200; //top level value - 5kHz pwm
	OCR1A = 3080; //100% duty cycle, A = right side
	OCR1B = 3200; //=left side
	TIFR1 |= 0x0F;
	//travels 5ft in 5.75seconds
	//slight veer left -- 3 inches per 5ft
}

void sharpright(){
	//Sets the right wheel backwards and the left wheel to forwards for a sharp turn
	OCR1A = 3080;
	PORTD &= ~(1<<PD2); //controls the right wheel to backwards
	PORTD &=~(1<<PD1); //controls the left wheel to forwards
	OCR1B = 3200;

	//343 units = 180 degree turn
}


void fd(int percent){
	//sets both wheels to a percentage of full speed in the forward direction
	OCR1A = 3080*percent;
	OCR1B = 3200*percent;
	PORTD |= 0x02;
	PORTD &= ~(1<<PD2);
}

void bd(int percent){
	//Sets both wheels to a percentage of full speed forward, then reverses the direction
	fd(percent);
	PORTD &=~(1<<PD1);
	PORTD |= 1<<PD2;
}


void sharpleft(){
	//Sets the left wheel to backwards and the right wheel forwards, for a sharp left turn
	OCR1A = 3080;
	PORTD |= (1<<PD1);
	PORTD |= (1<<PD2);
	OCR1B = 3200;
}

void delaytimerset(){ //sets up a delaytimer which overflows at 4ms
	TCCR2A = 0x00;
	TCCR2B = 0x06; //256 prescaler
}


float timefeet(float number){//converts a provided distance into the time delay needed
	return (575*number -100);
}


void delay2ms(float x){ //delay function which overflows at 4 ms, but halves the number of ms passed to it.
	x /=2;
	while(x>0){
		TIFR2 |= 0x0F;
		while(!(TCNT2 ==0));
		x--;
	}
}

void stop(){
	//stops both of the wheels
	OCR1A =0;
	OCR1B =0;
}


void rturn(){
	//slower right tun=rn, which scales the right wheel down based on the diameter of curve desired
	fd(100);
	OCR1A = 3080*.75; //70% = 15.5inch diameter
	//80% = 21 inch diameter
	//90% = 39 inch
}

void lturn(){
	//slower left tun=rn, which scales the left wheel down based on the diameter of curve desired
	fd(100);
	OCR1B = 3200*.75;
}


void IRsetup(){
	//max range 14.5 inch
	//pin D5 for compare b

	TCCR0A = 0x42;
	TCCR0B = 0x01; //no prescaler, CTC, clear pin on compare
	OCR0A = 209; //overflows at compare a, 13us later
	TIMSK0 = 0x02;
	TIFR0 |= 0x0F;
	TCNT0 = 0;
}

ISR(TIMER0_COMPA_vect){
	//every 13 us the ISR is called
	//Controls the timing of the on and off period of the IR, as well as the amount of time
	//since the last forward period of the searching mode
	irpos++;
	rotator++;
	if(irpos<30) //after 30*13us the 38kHz wave is shut off
		DDRD |= 1<<PD6;
	if(irpos == 28){ //at 28 counts, the value of the IR sensors are stored to prevent false IR readings
		irL = PIND & 1<<PD5;
		irR = PIND & 1<<PD7;
	}
	if(irpos>= 225) //after 3ms, reset the IR timing
			irpos = 0;
	else
		DDRD &=~(1<<PD6);
	if(rotator >= 230000) //reset the timing for the rotator after 3seconds
		rotator = 0;
}


void pcintset(){
	PCICR |= 0x01;
	PCMSK0 = 0x39; //interrupts for line sensing

}

ISR(PCINT0_vect){
//	FRsensor = (PINB & 1<<PB0);
//	FLsensor = (PINB & 1<<PB5);
//	BLsensor = (PINB & 1<<PB4);
//	BRsensor = (PINB & 1<<PB3);
	if((PINB & 1<<PB0)==0 || (PINB & 1<<PB5)==0){
		bd(100); //if one of the front sensors sees a white line, go in reverse
	}
	else if((PINB & 1<<PB3)==0 || (PINB & 1<<PB4)==0){
		fd(100); //if one of the back sensors sees a white line, go forward
}
	if((PINB & 1<<PB3)==0 || (PINB & 1<<PB0)==0){
		delay2ms(timefeet(1)); //if one of the right sensors was activated, turn left after one foot
		sharpleft();
		lastL = 0; //set the direction to search to the right
		lastR = 1;
		delay2ms(808/2-70); //turn 180 degrees
		fd(100);
	}
	else if((PINB & 1<<PB5)==0 || (PINB & 1<<PB4)==0){
		delay2ms(timefeet(1)); //if one of the left sensors was activated, turn right after one foot
		sharpright();
		lastL = 1;
		lastR = 0;
		delay2ms(808/2-70); //turn 180 degrees
		fd(100);
	}
	delay2ms(timefeet(.2));
	rotator=145000;
	mystate = searching;
}


void displaytest(){
	//function to test the lcd display attached through I2C
	//will display the favorite saying of Bender the bending bot from futurama
	DDRD |= 1<<PD3;
	PORTD |= 1<<PD3;
	lcd_init();
	lcd_print_string("Bite my shiny metal ass");
	while(1);
}


void motortest() {
	//tests the motors and motor code by having the robot move forward,
	// rotate one direction, then back the other,
	//and then move back to the original position
	DDRB |= 1<<PB2 | 1<<PB1;
	DDRD |= 1<<PD2 | (1<<PD1);
	PORTD |= 0x02;
	delaytimerset(); //sets up the timer for movement
	lcd_init();
	lcd_print_string("Bite my shiny metal ass");
	stop();
	delay2ms(2400);
	fivekHz();
	fd(100);
	delay2ms(timefeet(2)); //move forward 2 feet
	stop();
	delay2ms(400);
	sharpright();
	delay2ms(808 -70); //rotate 360 degrees to the right
	stop();
	delay2ms(400);
	sharpleft();
	delay2ms(808-70); //rotate 360 degrees to the left
	stop();
	delay2ms(400);
	bd(100);
	delay2ms(timefeet(2)); //back up 2 feet
	stop();
	while(1);
}


void irtest(){
	//function to test the ir sensors and transmitters
	//will turn on the LEDs attached to pin d3 if the sensors pick up an object
	DDRD = 0b01001000;
	sei();
	IRsetup();
	while(1){
		if((irR == 0)|| (irL ==0)){
			PORTD |= 1<<PD3; //turn on LED when the IR does not detect anything
		}
		else{
			PORTD &=~1<<PD3; //turn off LED
		}
	}
}


void freerunningtest(){
	lastL = lastR= 5;
	DDRB = 0b00000110;
	DDRD |= 1<<PD2 | (1<<PD1) | (1<<PD3); //outputs for the motors and LEDs
	DDRD &=~(1<<PD5); //IR receiver pins
	DDRD &=~(1<<PD7);
	PORTD |= 0xA2;
	sei();
	delaytimerset();
	lcd_init();
	fivekHz();
	stop();
	lcd_print_string("wait start"); //wait for official start time
	while(lastL != 0 && lastR != 0){
		while((PINB & 1<<PD5) == 0){
			lastL = 0; //decides the left or right as the first direction to turn based
		}              //based on the given start side of the operator
		while((PINB & 1<<PD7) == 0){
			lastR = 0;
		}
	}
	lcd_clear();
	lcd_print_string("Bite my shiny metal");
	IRsetup(); //initialize the IR sensors
	delay2ms(2441); //5 sec delay = 2441
	pcintset();
	mystate = searching; //start by searching for the other robot
	lcd_clear();
	rotator = 0;
	while(1){
		while(mystate == searching){
			if(rotator < 145000){ //while the loop is less than one full loop around
				PORTD &=~1<<PD3;
				if(lastL == 0)
					sharpleft();
				else if(lastR ==0)
					sharpright();
				lcd_goto_xy(0,0);
				lcd_print_string("CHARGE     ");
				if(irL == 0 || irR == 0){
					fd(100); //if it sees something then charge it
					mystate = approaching;
				}
			}
			else{
				fd(100);
				if(irL == 0 || irR == 0){ //if moving forward and the IR sees it then charge
					mystate = approaching;
					break;
				}
			}
		}
		if(mystate == approaching){
			PORTD |= 1<<PD3;
			lcd_goto_xy(0,0);
			lcd_print_string("sighted     ");
			while((irR == 0) || (irL ==0)){ //if either of the sensors are
				lastL = irL;
				lastR = irR;
				if((irR == 0) && (irL ==0)){
					fd(100);
				}
				else if((irR ==0))
					rturn();
				else if(irL ==0)
					lturn();

			}
			rotator=0;
			mystate = searching;
			PORTD &=~ 1<<PD3;
		}
	}
}


int main(void){ //pick the function to be run for the output desired
	freerunningtest();

	return 1;
}
