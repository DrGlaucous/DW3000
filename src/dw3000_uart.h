// wrapper for arduino's serial print functions so the decawave stuff can use it

#pragma once

// #include "dw3000.h"
#include <Arduino.h>

// multiple of these can be instantiated if we need them to be a member of multiple other classes
class DWUart
{
public:
	HardwareSerial &serialRef;

	// construct inital class, serial reference required
	DWUart(uint32_t baud = 115200) : serialRef(Serial)
	{
		Serial.begin(baud);
	}
	DWUart(HardwareSerial &newSerial, uint32_t baud = 115200) : serialRef(newSerial)
	{
		Serial.begin(baud);
	}

	// void UART_init(void); //originally started serial at 115200

	void putc(const char data)
	{
		Serial.print(data);
	}

	void puts(const char *s)
	{
		Serial.print(s);
	}

	void test_run_info(unsigned char *s)
	{
		puts((char *)s);
		puts("\r\n");
	}
};
