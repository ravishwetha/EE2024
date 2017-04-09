/*****************************************************************************
 *   A demo example using several of the peripherals on the base board
 *
 *   Copyright(C) 2011, EE2024
 *   All rights reserved.
 *
 ******************************************************************************/
#include <string.h>

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_uart.h"

#include "joystick.h"
#include "pca9532.h"
#include "acc.h"
#include "oled.h"
#include "rgb.h"
#include "light.h"
#include "temp.h"
#include "stdio.h"

static uint8_t barPos = 2;

static uint32_t msTicks = 0;
// This function is called every 1us
void SysTick_Handler(void) {
	msTicks++;
}

uint32_t getMsTicks() {
	return msTicks;
}

static void moveBar(uint8_t steps, uint8_t dir) {
	uint16_t ledOn = 0;

	if (barPos == 0)
		ledOn = (1 << 0) | (3 << 14);
	else if (barPos == 1)
		ledOn = (3 << 0) | (1 << 15);
	else
		ledOn = 0x07 << (barPos - 2);

	barPos += (dir * steps);
	barPos = (barPos % 16);

	pca9532_setLeds(ledOn, 0xffff);
}

static void drawOled(uint8_t joyState) {
	static int wait = 0;
	static uint8_t currX = 48;
	static uint8_t currY = 32;
	static uint8_t lastX = 0;
	static uint8_t lastY = 0;

	if ((joyState & JOYSTICK_CENTER) != 0) {
		oled_clearScreen(OLED_COLOR_BLACK);
		return;
	}

	if (wait++ < 3)
		return;

	wait = 0;

	if ((joyState & JOYSTICK_UP) != 0 && currY > 0) {
		currY--;
	}

	if ((joyState & JOYSTICK_DOWN) != 0 && currY < OLED_DISPLAY_HEIGHT - 1) {
		currY++;
	}

	if ((joyState & JOYSTICK_RIGHT) != 0 && currX < OLED_DISPLAY_WIDTH - 1) {
		currX++;
	}

	if ((joyState & JOYSTICK_LEFT) != 0 && currX > 0) {
		currX--;
	}

	if (lastX != currX || lastY != currY) {
		oled_putPixel(currX, currY, OLED_COLOR_WHITE);
		lastX = currX;
		lastY = currY;
	}
}

#define NOTE_PIN_HIGH() GPIO_SetValue(0, 1<<26);
#define NOTE_PIN_LOW()  GPIO_ClearValue(0, 1<<26);

static uint32_t notes[] = { 2272, // A - 440 Hz
		2024, // B - 494 Hz
		3816, // C - 262 Hz
		3401, // D - 294 Hz
		3030, // E - 330 Hz
		2865, // F - 349 Hz
		2551, // G - 392 Hz
		1136, // a - 880 Hz
		1012, // b - 988 Hz
		1912, // c - 523 Hz
		1703, // d - 587 Hz
		1517, // e - 659 Hz
		1432, // f - 698 Hz
		1275, // g - 784 Hz
		};

static void playNote(uint32_t note, uint32_t durationMs) {

	uint32_t t = 0;

	if (note > 0) {

		while (t < (durationMs * 1000)) {
			NOTE_PIN_HIGH()
			;
			Timer0_us_Wait(note / 2);
			//delay32Us(0, note / 2);

			NOTE_PIN_LOW()
			;
			Timer0_us_Wait(note / 2);
			//delay32Us(0, note / 2);

			t += note;
		}

	} else {
		Timer0_Wait(durationMs);
		//delay32Ms(0, durationMs);
	}
}

static uint32_t getNote(uint8_t ch) {
	if (ch >= 'A' && ch <= 'G')
		return notes[ch - 'A'];

	if (ch >= 'a' && ch <= 'g')
		return notes[ch - 'a' + 7];

	return 0;
}

static uint32_t getDuration(uint8_t ch) {
	if (ch < '0' || ch > '9')
		return 400;

	/* number of ms */

	return (ch - '0') * 200;
}

static uint32_t getPause(uint8_t ch) {
	switch (ch) {
	case '+':
		return 0;
	case ',':
		return 5;
	case '.':
		return 20;
	case '_':
		return 30;
	default:
		return 5;
	}
}

static void playSong(uint8_t *song) {
	uint32_t note = 0;
	uint32_t dur = 0;
	uint32_t pause = 0;

	/*
	 * A song is a collection of tones where each tone is
	 * a note, duration and pause, e.g.
	 *
	 * "E2,F4,"
	 */

	while (*song != '\0') {
		note = getNote(*song++);
		if (*song == '\0')
			break;
		dur = getDuration(*song++);
		if (*song == '\0')
			break;
		pause = getPause(*song++);

		playNote(note, dur);
		//delay32Ms(0, pause);
		Timer0_Wait(pause);

	}
}

static uint8_t * song = (uint8_t*) "C2.C2,D4,C4,F4,E8,";
//(uint8_t*)"C2.C2,D4,C4,F4,E8,C2.C2,D4,C4,G4,F8,C2.C2,c4,A4,F4,E4,D4,A2.A2,H4,F4,G4,F8,";
//"D4,B4,B4,A4,A4,G4,E4,D4.D2,E4,E4,A4,F4,D8.D4,d4,d4,c4,c4,B4,G4,E4.E2,F4,F4,A4,A4,G8,";

static void init_ssp(void) {
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}

static void init_i2c(void) {
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_GPIO(void) {
	// Initialize button
	//SW4
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 0;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 1;
	PinCfg.Pinnum = 31;

	PINSEL_ConfigPin(&PinCfg);

	GPIO_SetDir(1, 1 << 31, 0);

	//SW3 (BL_EN)
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 4;

	PINSEL_ConfigPin(&PinCfg);

	GPIO_SetDir(0, 1 << 4, 0);

	//Red LED
	PinCfg.Pinmode = 1;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 0;

	PINSEL_ConfigPin(&PinCfg);

	GPIO_SetDir(2, 1 << 0, 1);
}

void pinsel_uart3(void) {
	PINSEL_CFG_Type PinCfg;
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 0;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 1;
	PINSEL_ConfigPin(&PinCfg);
}

void init_uart(void) {
	UART_CFG_Type uartCfg;
	uartCfg.Baud_rate = 115200;
	uartCfg.Databits = UART_DATABIT_8;
	uartCfg.Parity = UART_PARITY_NONE;
	uartCfg.Stopbits = UART_STOPBIT_1;
	//pin select for uart3;
	pinsel_uart3();
	//supply power & setup working parameters for uart3
	UART_Init(LPC_UART3, &uartCfg);
	//enable transmit for uart3
	UART_TxCmd(LPC_UART3, ENABLE);
}

void stable_state_config() {
	//segment display off
	led7seg_setChar(' ', FALSE);
	GPIO_ClearValue(2, 0);
	GPIO_ClearValue(0, (1 << 26));
	oled_clearScreen(OLED_COLOR_BLACK);
}

static char* msg = NULL;
int main(void) {
	SysTick_Config(SystemCoreClock / 1000);  // every 1ms;
	uint8_t dir = 1;
	uint8_t wait = 0;
	uint8_t state = 0;

	init_i2c();
	init_ssp();
	init_GPIO();

	pca9532_init();
	joystick_init();
	acc_init();
	oled_init();
	led7seg_init();
	rgb_init();

	light_enable();
	light_setRange(LIGHT_RANGE_4000);
	unsigned int lightValue = 0;

	temp_init(&getMsTicks);

	/* uart setup */
	uint8_t data = 0;
	uint32_t len = 0;
	uint8_t line[64];

	init_uart();

	//test sending message
	msg = "Welcome to EE2024 \r\n";
	UART_Send(LPC_UART3, (uint8_t *) msg, strlen(msg), BLOCKING);

	/*
	 * Assume base board in zero-g position when reading first value.
	 */
	int8_t x = 0;
	int8_t y = 0;
	int8_t z = 0;
	int32_t xoff = 0 - x;
	int32_t yoff = 0 - y;
	int32_t zoff = 64 - z;
	acc_read(&x, &y, &z);

	/* ---- Speaker ------> */

	GPIO_SetDir(2, 1 << 0, 1);
	GPIO_SetDir(2, 1 << 1, 1);

	GPIO_SetDir(0, 1 << 27, 1);
	GPIO_SetDir(0, 1 << 28, 1);
	GPIO_SetDir(2, 1 << 13, 1);
	GPIO_SetDir(0, 1 << 26, 1);

	GPIO_ClearValue(0, 1 << 27); //LM4811-clk
	GPIO_ClearValue(0, 1 << 28); //LM4811-up/dn
	GPIO_ClearValue(2, 1 << 13); //LM4811-shutdn

	/* <---- Oled ------ */
	moveBar(1, dir);
	oled_clearScreen(OLED_COLOR_BLACK);

	int segment_display = 0;
	char led7char = segment_display;

	/* <---- Buttons and LEDs ------ */
	uint8_t btn1 = 1;
	uint8_t RED_LED = 0;

	/* <---- Mode flag ------ */
	uint8_t MODE = 0;
	uint8_t MODE_ENTERED = 1;

	while (1) {
		// MODE TOGGLE
		btn1 = (GPIO_ReadValue(1) >> 31) & 0x01;
		if (btn1 == 0) {
			MODE = !MODE;
			MODE_ENTERED = 1;
		}

		if (MODE == 0) {
			if(MODE_ENTERED == 1) {
				printf("STABLE STATE\n");
				stable_state_config();
				MODE_ENTERED = 0;
			}
		} else {
			if (MODE_ENTERED == 1) {
				printf("MONITOR MODE\n");
				msg = "Entering MONITOR MODE.\r\n";
				UART_Send(LPC_UART3, (uint8_t *) msg, strlen(msg), BLOCKING);
				led7seg_setChar(led7char, FALSE);
				MODE_ENTERED = 0;
			}

			/* ####### Accelerometer and LEDs  ###### */
			/* # */
			lightValue = light_read();
			//printf("light value %u \n", lightValue);

			uint8_t oled_for_light[40];
			if (lightValue < 1000)
				sprintf(oled_for_light, "LIGHT: %ld ", lightValue);
			else
				sprintf(oled_for_light, "LIGHT: %ld", lightValue);
			oled_putString(20, 20, (uint8_t *) oled_for_light, OLED_COLOR_WHITE,
					OLED_COLOR_BLACK);

			/* Temp sensor */
			//printf("Temperature is: %f\n", temp_read()/10.0);
			acc_read(&x, &y, &z);
			x = x + xoff;
			y = y + yoff;
			z = z + zoff;

			if (y < 0) {
				dir = 1;
				y = -y;
			} else {
				dir = -1;
			}

			if (y > 1 && wait++ > (40 / (1 + (y / 10)))) {
				moveBar(1, dir);
				wait = 0;
			}

			//printf("Acc x, y, z: %d, %d, %d:\n", x, y, z);
			//printf("Agg total: %d\n", x*x+y*y+z*z);

			/* # */
			/* ############################################# */

			/* ####### Joystick and OLED  ###### */
			/* # */

			state = joystick_read();
			if (state != 0)
				drawOled(state);

			/* # */
			/* ############################################# */

			//  LED1 = (GPIO_ReadValue(2)>> 0) & 0x01;
			/* ############ Trimpot and RGB LED  ########### */
			/* # */

			// FIO_SetValue (2, 1<<0); // RED
			// FIO_SetValue (2, 1<<1);	// GREEN
			FIO_SetValue(0, 1 << 26);

			//  SEGMENT_DISPLAY
			if (getMsTicks() % 1000 < 100) {
				if (segment_display < 10) {
					led7char = segment_display;
				} else {
					switch (segment_display) {
					case 10:
						led7char = 'A';
						break;
					case 11:
						led7char = 'B';
						break;
					case 12:
						led7char = 'C';
						break;
					case 13:
						led7char = 'D';
						break;
					case 14:
						led7char = 'E';
						break;
					case 15:
						led7char = 'F';
						break;
					default:
						led7char = 'H';
					}
				}
				segment_display++;
			}
		}
		Timer0_Wait(1);
	}

}

void check_failed(uint8_t *file, uint32_t line) {
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while (1)
		;
}

