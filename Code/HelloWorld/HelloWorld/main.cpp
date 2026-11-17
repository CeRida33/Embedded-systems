/*
 * HelloWorld.cpp
 *
 * Created: 20/10/2026 11:00:09
 * Author : celine
 */ 

#include <avr/io.h>


int main(void)
{
	DDRD |= (1<<DDD6); //PD6 som utgang
	PORTD &= ~(1 << PORTD6); //Skriv lav (0v) til PD6
	
    while (1) 
    {
    }
}

