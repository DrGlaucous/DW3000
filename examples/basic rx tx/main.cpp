#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "SPI.h"
#include "packet.h"

DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

//how the radio should be configured for the session.
static const dwt_config_t config = {
    5,                		/* Channel number. */
    DWT_PLEN_128,     		/* Preamble length. Used in TX only. */
    DWT_PAC8,         		/* Preamble acquisition chunk size. Used in RX only. */
    9,                		/* TX preamble code. Used in TX only. */
    9,                		/* RX preamble code. Used in RX only. */
    1,                		/* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       		/* Data rate. */
    DWT_PHRMODE_STD,  		/* PHY header mode. */
    DWT_PHRRATE_STD,  		/* PHY header rate. */
    (129 + 8 - 8),    		/* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, 		/* STS disabled */
    DWT_STS_LEN_64,   		/* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       		/* PDOA mode off */
};

//I'll keep both of these here for reference.
dwt_txconfig_t txconfig_ch5 =
{
    0x34,           /* PG delay. */
    0xfdfdfdfd,      /* TX power. */
    0x0             /*PG count*/
};

dwt_txconfig_t txconfig_ch9 =
{
    0x34,           /* PG delay. */
    0xfefefefe,     /* TX power. */
    0x0             /*PG count*/
};



void setup()
{

	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(115200);
	Serial.println("Begin");


	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();


	//set up the backend components and feed them into the main DW3000 class
	uart = new DWUart(115200);
	port = new DW3000Port(&SPI, SPI_CS, DW_RST, DW_IRQ);
	radio = new DW3000(uart, port);

	//hard reset
	port->reset();

	radio->dwt_softreset();

	while (!radio->dwt_checkidlerc()) // Need to make sure DW IC is in IDLE_RC before proceeding
	{
		Serial.println("Idle failed");
		delay(1000);
	}
	
	// uses DWT_LOADUCODE, which we don't have
	if (radio->dwt_initialise(0) == DWT_ERROR)
	{
		while (1)
		{
			Serial.println("Init failed");
			delay(1000);
		};
	}

	//enabling LEDs 2 and 3 (visible on the eval board) to blink on RX and TX
  	radio->dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

	//manual LED control
	//radio->gpio_init_output();


	while (radio->dwt_configure(&config))
	{
		Serial.println("Config failed");
		delay(1000);
	}
	
	//reset radio state: not doing this here results in a ~17s delay from the internal 40 bit sys timer
	//if we do a soft reset, we don't need to do this.
	//radio->dwt_writefastCMD(CMD_TXRXOFF);

	//only really needed if we're doing TX, it configures the phased locked loop, TX power, and delay settings
	//doing it for an rx-only radio shouldn't hurt.
	radio->dwt_configuretxrf(&txconfig_ch5);
	

	//configure sleep settings
	radio->dwt_configuresleep(
		DWT_CONFIG //on wakeup, download the always-on register values to the "HIF"
		| DWT_PGFCAL, //re-calibrate PGF on wake

		DWT_PRES_SLEEP //preserve the SLEEP_EN bit, clears on wakeup
		| DWT_WAKE_CSN //wake up using the chip select line
		| DWT_WAKE_WUP //wake up using the wakeup pin
		| DWT_SLP_EN //enable sleep functionality
		//| DWT_SLEEP//adding this will use normal sleep mode instead of deep sleep mode
	);


	Serial.println("Ready");

}

//initiator
void loop() {

	//make a packet to send
	auto payload = "Pay Zone";
	UWBPacket packet = UWBPacket(
		0x1122334455667788,
		0x8877887788778877,
		PacketType::UserDefined,
		(const uint8_t*)payload,
		sizeof(payload)
	);


	//write actual data to the outgoing buffer, automatically accounts for >127 packet sizes
	radio->dwt_writetxdata(packet.get_compiled_len(), packet.get_compiled(), 0);

	//append FC data (2 byte checksum), identify this packet as a ranging packet
	radio->dwt_writetxfctrl(packet.get_compiled_len() + FCS_LEN, 0, 1);

	//start TX mode
	//essentially just does: radio->dwt_writefastCMD(CMD_TX);
	radio->dwt_starttx(DWT_START_TX_IMMEDIATE);

	//check for a successful transmit with timeout
	bool send_error = true;
	for(int i = 0; i < 50; ++i) {
		if(radio->check_frame_tx_success()) {
			send_error = false;
			break;
		}
	}


	//go to sleep and enter idle state when woken up
	radio->dwt_entersleep(DWT_DW_IDLE);

	Serial.print("Frame sent with error: ");
	Serial.println(send_error);

	delay(1000);

	//we want this to return an error, that way we know it was sleeping
	//Serial.print("ID check sleep: ");
	//Serial.println(radio->dwt_check_dev_id());

	//wakeup sequence
	{
		radio->dwt_wakeup_ic();

		//wait for radio to stabilize
		while(!radio->check_for_idle()) {}

		//restore the rest of the configuration not preserved by sleep.
		//the example code I looked at seemed to have problems with this, hardcoding its own restoreconfig function,
		//but in my little example here, it works (when paired with loop_rx below)
		radio->dwt_restoreconfig();
	}

	//Serial.print("ID check awake: ");
	//Serial.println(radio->dwt_check_dev_id());	
}

//responder
bool flip = false;
void loop_rx()
{

	//start RX mode
	//this essentially just does: radio->dwt_writefastCMD(CMD_RX);
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

	uint32_t rx_result = 0;
	do {
		rx_result = radio->check_for_rx();
	} while(rx_result == 0);

	switch(rx_result) {

		case 1: //success
		{
			flip = !flip;
			radio->gpio_set(2, flip);
			radio->gpio_set(3, !flip);

			//get size of frame and read it in (minus CRC)
			uint32_t frame_length = radio->get_frame_length() - FCS_LEN;
			uint8_t frame_data[frame_length] = {};
			radio->dwt_readrxdata(frame_data, frame_length, 0);

			UWBPacket packet = UWBPacket(frame_data, frame_length);

			Serial.print("Success. Frame len: ");
			Serial.print(frame_length);
			Serial.print(" Packet Payload: ");
			Serial.print((const char*)packet.get_payload());
			Serial.print(" Src MAC: ");
			Serial.print(uint32_t(packet.get_source_uuid() >> 32), HEX);
			Serial.println(uint32_t(packet.get_source_uuid() & 0xFFFFFFFF), HEX);
			break;
		}
		case 2: //bad checksum
		{
			Serial.println("Bad Checksum");
			break;
		}
		case 3: //error
		{
			Serial.println("RX Error");
			break;
		}
		default: //should not reach this point.
			break;
	}
	//clear status, start again
	radio->clear_system_status();


}






