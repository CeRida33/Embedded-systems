/*
 * Soft_blink.cpp
 *
 * Created: 20/10/2026 14:29:48
 * Author : celine
 */ 

#include <avr/io.h>
#define F_CPU 1000000UL              // Må stemme med faktisk klokke, ellers blir _delay_ms feil
#include <avr/io.h>
#include <avr/pgmspace.h>            // PROGMEM og pgm_read_byte
#include <util/delay.h>              // _delay_ms

// Gammakorrigert tabell: gamma8[x] = round(255 * (x/255)^2.2)
// PROGMEM legger tabellen i flash (16 KB) i stedet for SRAM (bare 1 KB).
static const uint8_t gamma8[256] PROGMEM = {
	0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
	1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
	3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
	6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
	12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
	20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
	30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
	42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
	56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
	73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
	91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
	113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
	137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
	163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
	192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
	223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255
};

static void pwm_init(void)
{
	DDRD |= (1 << DDD6);                       // OC0A (PD6) som utgang

	TCCR0A = (1 << COM0A1) | (1 << COM0A0)     // Inverterende: større OCR0A = mer lys (sink-LED)
	| (1 << WGM01)  | (1 << WGM00);     // Fast PWM, TOP = 0xFF
	TCCR0B = (1 << CS01);                      // Prescaler 8 -> ? 488 Hz ved 1 MHz

	OCR0A = 0;
}                     

int main(void)
{
	pwm_init();

	while (1)
	{
		// Opp: 256 steg x 8 ms ? 2 s
		// uint16_t fordi en uint8_t aldri blir 256, og løkka ville gått evig
		for (uint16_t i = 0; i < 256; i++) {
			OCR0A = pgm_read_byte(&gamma8[i]);   // Les fra flash, ikke direkte gamma8[i]
			_delay_ms(8);
		}

		// Ned: ? 2 s
		for (int16_t i = 255; i >= 0; i--) {
			OCR0A = pgm_read_byte(&gamma8[i]);
			_delay_ms(8);
		}
	}
}

