#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
#include "SPI.h"
#include "packet.h"


//we need to calibrate the board in order to get these
#define RX_ANT_DELAY 16385
#define TX_ANT_DELAY 16385

//it's up to the MC to accept or reject packets based on this.
#define RX_MAC 0x1122334455667788
#define DEST_MAC 0x8877887788778877


DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

//how the radio should be configured for the session.
static const dwt_config_t config = {
    5,                		/* Channel number. */
    DWT_PLEN_64,     		/* Preamble length. Used in TX only. */
    DWT_PAC8,         		/* Preamble acquisition chunk size. Used in RX only. */
    9,                		/* TX preamble code. Used in TX only. */
    9,                		/* RX preamble code. Used in RX only. */
    1,                		/* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       		/* Data rate. */
    DWT_PHRMODE_STD,  		/* PHY header mode. */
    DWT_PHRRATE_STD,  		/* PHY header rate. */
    (64 + 1 + 8 - 8),    	/* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
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


bool flip = false;



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
  	//radio->dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

	//manual LED control
	radio->gpio_init_output();


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
	
	//just some quick-n-dirty pre-calculated values for 64 MHz PRF (pulled from an example file)
	//these should be calibrated when we make real code
	radio->dwt_setrxantennadelay(RX_ANT_DELAY);
	radio->dwt_settxantennadelay(TX_ANT_DELAY);


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

	//start TX mode, send data and immediately switch to RX mode
	//essentially just does: radio->dwt_writefastCMD(CMD_TX_W4R);
	radio->dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

	//check for a successful transmit with timeout
	bool send_error = true;
	for(int i = 0; i < 50; ++i) {
		if(radio->check_frame_tx_success()) {
			send_error = false;
			break;
		}
	}

	//had problem sending packet, reset radio status and try to send again
	if(send_error) {
		radio->clear_system_status();
		radio->dwt_writefastCMD(CMD_TXRXOFF);
		Serial.println("Send Error");
		return;
	}


	//wait for 1 second to get a packet back
	bool got_response = false;
	auto tx_time = millis();
	while(millis() - tx_time < 1000) {
		bool error = false;
		auto response = radio->check_for_rx();
		switch(response) {

			case 1: //got correct packet
			{
				got_response = true;
				break;
			}
			case 2:
			case 3: //some error, we got a packet, but we need to try again; it was corrupted
			{
				error = true;
				break;
			}
		}
		//break ouf of while loop
		if(error || got_response) {
			break;
		}
	}



	//perform actions based on response
	if(got_response) {
		flip = !flip;
		radio->gpio_set(2, flip);
		radio->gpio_set(3, !flip);


		//parse the packet to get the timestamps from the other radio
		//get size of frame and read it in (minus CRC)
		uint32_t frame_length = radio->get_frame_length() - FCS_LEN;
		uint8_t frame_data[frame_length] = {};
		radio->dwt_readrxdata(frame_data, frame_length, 0);

		UWBPacket packet = UWBPacket(frame_data, frame_length);

		switch(packet.get_packet_type()) {
			case PacketType::Ranging: {

				RangingPacket ranging_data = RangingPacket(packet.get_payload());

				//timestamp when the initial packet was sent by us
				uint64_t tx_timestamp = radio->get_tx_timestamp_u64();

				//timestamp when the response was got by us
				uint64_t rx_timestamp = radio->get_rx_timestamp_u64();
				
				//timestamp when the initial packet was got by remote
				auto remote_rx_timestamp = ranging_data.get_rx_time(); 

				//timestamp when the remote was sent
				auto remote_tx_timestamp = ranging_data.get_tx_time();

				//get the clock offset of remote (relates to the signal phase, I.E. PDOA) in parts per million
				//reads and returns the 13 bit COE_PPM value from CIA_DIAG_0, sign extended into a u16
				//we need to divide this down as mentioned on page 184.
				float clock_offset_ratio = (float)radio->dwt_readclockoffset() / (float)(uint32_t)(1<<26);


				//should now have everything we need to perform ranging
				
				//total times, compare the difference between them and divide it by 2 to get a one-way TOF
				double remote_timespan = (remote_tx_timestamp - remote_rx_timestamp) * (1.0 - clock_offset_ratio);
				double local_timespan = (rx_timestamp - tx_timestamp);

				double time_of_flight = ((double)local_timespan - (double)remote_timespan) / 2.0;
				double distance = time_of_flight * SPEED_OF_LIGHT * DWT_TIME_UNITS;

				Serial.print("Distance: ");
				Serial.println(distance);


				break;
			}
			default: {
				Serial.println("Wrong RX Packet type. Expected Ranging packet");
			}
		}

		

		//Serial.println("Got Response");
	} else {
		Serial.println("RX timeout or error");
	}
	

	//reset for next time
	radio->dwt_writefastCMD(CMD_TXRXOFF);
	radio->clear_system_status();	

	delay(200);


}


//responder
void loop_rx()
{

	//start RX mode
	//this essentially just does: radio->dwt_writefastCMD(CMD_RX);
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

	//wait for result
	uint32_t rx_result = 0;
	do {
		rx_result = radio->check_for_rx();
	} while(rx_result == 0);

	switch(rx_result) {

		case 1: //success
		{

			//get size of frame and read it in (minus CRC)
			uint32_t frame_length = radio->get_frame_length() - FCS_LEN;
			uint8_t frame_data[frame_length] = {};
			radio->dwt_readrxdata(frame_data, frame_length, 0);

			//the actual contents of this packet shouldn't matter, it's the inital ping
			UWBPacket in_packet = UWBPacket(frame_data, frame_length);

			//the time we got the packet
			auto rx_timestamp = radio->get_rx_timestamp_u64();

			//time it takes from getting a frame to sending one back out again
			//if we get rx timeouts on the initiator, it's probably because this isn't long enough. (we miss the window and have to wait for the clock to cycle back around again, which is beyond the 1 second delay of the host)
			uint32_t turnaround_time_us = 1000;

			//add all this together to know what time the new packet will leave. I have no idea what the bit shifting does; the original code also had that.
			//(I guess it reduces precision in order to gain length since the normal timestamps are 5 bytes long, but this one is only 4, so we get the MSB)
			//it also says the LSB is ignored here, so we mask that out. see page 87
			uint64_t tx_timestamp = ((turnaround_time_us * UUS_TO_DWT_TIME + rx_timestamp) >> 8) & 0xFFFFFFFEUL;
			radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);

			//shift it back and add our calibrated antenna delay (the other example puts the 0xFEUL mask here, but we did that above)
			tx_timestamp = (tx_timestamp << 8) + TX_ANT_DELAY;

			//construct a ranging packet to respond with
			RangingPacket range_p = RangingPacket(rx_timestamp, tx_timestamp);

			//test
			//RangingPacket range_p = RangingPacket(0xAAFFFFFEEFFFFFAA, 0xBBFFFFF44FFFFFBB);

			UWBPacket outgoing = UWBPacket(0x8877665544332211, 0x8877665544332211, PacketType::Ranging, range_p.get_compiled(), range_p.get_compiled_len());

			//write packet
			radio->dwt_writetxdata(outgoing.get_compiled_len(), outgoing.get_compiled(), 0);

			//append FC data (2 byte checksum), identify this packet as a ranging packet for the radio (that '1' at the end)
			radio->dwt_writetxfctrl(outgoing.get_compiled_len() + FCS_LEN, 0, 1);

			//send packet out on delay, it will wait until the clock hits exactly the value we set in dwt_setdelayedtrxtime. If we missed it, it will overflow back in roughly 17 seconds
			int send_result = radio->dwt_starttx(DWT_START_TX_DELAYED);

			if(send_result == DWT_SUCCESS) {

				//wait until send
				while(!radio->check_frame_tx_success()) {}

				Serial.println("RX send success");
			
				flip = !flip;
				radio->gpio_set(2, flip);
				radio->gpio_set(3, !flip);
			}

			break;
		}
		case 2: //bad checksum
		{
			radio->gpio_set(2, 0);
			radio->gpio_set(3, 0);
			Serial.println("Bad Checksum");
			break;
		}
		case 3: //error
		{
			radio->gpio_set(2, 0);
			radio->gpio_set(3, 0);
			Serial.println("RX Error");
			break;
		}
		default: //should not reach this point.
			break;
	}
	//clear status, start again
	radio->clear_system_status();


}






