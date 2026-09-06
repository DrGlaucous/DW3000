// wrapper for arduino's serial print functions so the decawave stuff can use it

#pragma once

// #include "dw3000.h"
#include <Arduino.h>

//a dummy class that implements Stream, but throws its data into the void
class DummyStream : public Stream {
	public:

	DummyStream() {}

    int available() {
		return 0;
	};

    int read() {
		return -1;
	};

    int peek() {
		return -1;
	};

    void flush() {};

	size_t write(uint8_t data) {
		return 1;
	};


};

// multiple of these can be instantiated if we need them to be a member of multiple other classes
class DWUart
{
public:
	Stream &serialRef;

	//construct this class with hardwareSerial and a baud rate
	DWUart(uint32_t baud = 115200) : serialRef(Serial)
	{
		Serial.begin(baud);
	}

	//construct this class using a generic stream object (if it's a Serial object, it needs to be started externally first!)
	DWUart(Stream &newSerial) : serialRef(newSerial)
	{}

	void putc(const char data)
	{
		serialRef.print(data);
	}

	void puts(const char *s)
	{
		serialRef.print(s);
	}

	void test_run_info(unsigned char *s)
	{
		puts((char *)s);
		puts("\r\n");
	}
};
