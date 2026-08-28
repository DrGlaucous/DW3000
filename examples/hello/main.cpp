#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "SPI.h"

DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

void setup()
{

	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(115200);

	Serial.println("Begin");

	//while(1) {
	//	Serial.println("AA");
	//	delay(1000);
	//}

	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();

	// pinMode(SPI_CS, OUTPUT);
	// digitalWrite(SPI_CS, LOW);
	// SPI.transfer(0); 

	// auto a = SPI.transfer(0); 
	// auto b = SPI.transfer(0); 
	// auto c = SPI.transfer(0); 
	// auto d = SPI.transfer(0); 

	// Serial.print(a, HEX);
	// Serial.print(b, HEX);
	// Serial.print(c, HEX);
	// Serial.println(d, HEX);

	// digitalWrite(SPI_CS, HIGH);


	// sets our CS and interrupt pins, as well as does other radio-inity stuff
	// spiBegin(DW_IRQ, DW_RST);
	// spiSelect(SPI_CS);

	// // we'll do that ourself with the direct API since that's the easiest way to mimic the MULoc code.;
	// pinMode(SPI_CS, OUTPUT);
	// digitalWrite(SPI_CS, true); // disable

	// uint32_t output = 0;
	// auto port = DW3000Port(SPI, SPI_CS, DW_RST, DW_IRQ);
	// port.readBytes(0, 0, (uint8_t*)&output, 4);

	// Serial.println(output);

	// while (1)
	// {
	// 	Serial.println("Wait");
	// 	delay(1000);
	// };


	//eew references. Debating if I should purge the SPI reference from DW3000Port
	uart = new DWUart(115200);
	port = new DW3000Port(&SPI, SPI_CS, DW_RST, DW_IRQ);
	radio = new DW3000(uart, port);


	port->reset();
	
	// uses DWT_LOADUCODE, which we don't have
	if (radio->dwt_initialise(0) == DWT_ERROR)
	{
		while (1)
		{
			Serial.println("Error");
			delay(1000);
		};
	}


	radio->gpio_init_output();

	// auto gpio_mode = radio->dwt_read32bitoffsetreg(GPIO_MODE_ID, 0);
	// Serial.println(gpio_mode, HEX);
	// //set GPIO pins 0 through 3 as output
	// radio->dwt_and8bitoffsetreg(GPIO_DIR_ID, 0, 0xF0);
	// //radio->dwt_setleds(1);
	// //write LEDs 3 and 2 high
	// radio->dwt_or8bitoffsetreg(GPIO_OUT_ID, 0, 0b1100);

	// gpio_mode = radio->dwt_read32bitoffsetreg(GPIO_DIR_ID, 0);
	// Serial.println(gpio_mode, HEX);
	// gpio_mode = radio->dwt_read32bitoffsetreg(GPIO_OUT_ID, 0);
	// Serial.println(gpio_mode, HEX);



	// dwt_loadopsettabfromotp

	// dwt_configure();
	// dwt_configuretxrf();

	// dwt_setleds();

	// dwt_setpanid();

	// dwt_setaddress16();

	// dwt_setrxantennadelay();
	// dwt_settxantennadelay();
}

bool flip = false;
void loop()
{

	if (radio->dwt_check_dev_id() == DWT_SUCCESS)
	{
		//Serial.println("Found ID");
		uart->puts("Found ID\n");
	}
	else
	{
		//Serial.println("Did not find ID");
		uart->puts("Did not find ID\n");
	}
	delay(1000);


	radio->gpio_set(2, flip);
	radio->gpio_set(3, !flip);
	flip = !flip;
}
