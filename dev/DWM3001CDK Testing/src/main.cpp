//DSTWR example code
//three-way calibration code, used to get the rx and tx delay of a single radio.
//requires 3 radios for the setup
//see: https://forum.qorvo.com/t/alternative-approach-for-antenna-delay-calibration/16284


#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
#include "SPI.h"
#include "packet.h"



DummyStream* dummy = nullptr;
DWUart* uart = nullptr;
DW3000Port* port = nullptr;
DW3000* radio = nullptr;

//how the radio should be configured for the session.
static const dwt_config_t config_ch5 = {
    5,                		/* Channel number. */
    DWT_PLEN_64,     		/* Preamble length. Used in TX only. */
    DWT_PAC8,         		/* Preamble acquisition chunk size. Used in RX only. */
    10,                		/* TX preamble code. Used in TX only. */
    10,                		/* RX preamble code. Used in RX only. */
    1,                		/* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       		/* Data rate. */
    DWT_PHRMODE_STD,  		/* PHY header mode. */
    DWT_PHRRATE_STD,  		/* PHY header rate. */
    (64 + 1 + 8 - 8),    	/* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, 		/* STS disabled */
    DWT_STS_LEN_64,   		/* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0       		/* PDOA mode off */
};

static const dwt_config_t config_ch9 = {
    9,                		/* Channel number. */
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

dwt_txconfig_t txconfig_ch5 = {
    0x34,           /* PG delay. */
    0xfdfdfdfd,      /* TX power. */
    0x0             /*PG count*/
};

dwt_txconfig_t txconfig_ch9 = {
    0x34,           /* PG delay. */
    0xfefefefe,     /* TX power. */
    0x0             /*PG count*/
};


bool flip = false;

/////////////////////helper functions

//platform specific code, this will need to be changed for non-NRF devices!
//MAC address should be hard-coded into each device
uint64_t get_uuid() {

	//collect both halves of the unique device ID
	uint64_t lsb = (uint64_t)NRF_FICR->DEVICEID[0];
	uint64_t msb = (uint64_t)NRF_FICR->DEVICEID[1];

	//merge them together and return them as one chunk
	return (msb << 32) | lsb;
}

//prints a u64, since the arduino IDE's serial.print doesn't handle this
void print_u64(uint64_t value, int base) {
	Serial.print((uint32_t)(value >> 32), base);
	Serial.print((uint32_t)(value & 0xFFFFFFFF), base);
}

//writes a UWBPacket out to the radio and waits for it to respond with the send status
//if switch_to_rx is true, the radio will immediately go into RX mode after transmission (a reset fast command needs to be sent to get out of this mode)
bool send_packet(UWBPacket& packet, uint8_t mode) {
	
	//write actual data to the outgoing buffer, automatically accounts for >127 packet sizes
	radio->dwt_writetxdata(packet.get_compiled_len(), packet.get_compiled(), 0);

	//append FC data (2 byte checksum), identify this packet as a ranging packet
	radio->dwt_writetxfctrl(packet.get_compiled_len() + FCS_LEN, 0, 1);

	//start TX mode
	int first_send_error = radio->dwt_starttx(mode);

	//happens if the delayed time has passed, it puts the radio into off mode
	if(first_send_error != DWT_SUCCESS) {
		//Serial.print("Delay send error: ");
		//Serial.println(first_send_error);
		return false;
	}

	//check for a successful transmit with timeout
	bool send_error = true;
	//for(int i = 0; i < 50; ++i) {
	pinMode(14, OUTPUT);
	digitalWrite(14, false);
	while(1) {
		if(radio->check_frame_tx_success()) {
			send_error = false;
			break;
		}
	}
	digitalWrite(14, true);

	//had problem sending packet, reset radio status and return
	if(send_error) {
		radio->clear_system_status();
		radio->dwt_writefastCMD(CMD_TXRXOFF);
		//Serial.println("Send Error");
		return false;
	}

	return true;

}

//reads the last gotten data as a UWB packet out of the radio's internal buffer
UWBPacket get_packet() {


	//parse the packet to get the timestamps from the other radio
	//get size of frame and read it in (minus CRC)
	uint32_t frame_length = radio->get_frame_length() - FCS_LEN;
	uint8_t frame_data[frame_length] = {};
	radio->dwt_readrxdata(frame_data, frame_length, 0);
	return UWBPacket(frame_data, frame_length);

}

//blocks until the radio gets a message in or until timeout_ms is reached.
//the radio must already be set to the correct mode with a fast command!
//returns 0 on success, 1 on timeout, -1 on error
int wait_for_message_with_timeout(uint32_t timeout_ms, bool no_timeout = false) {


	bool got_response = false;
	bool error = false;
	auto tx_time = millis();
	while(no_timeout || (millis() - tx_time < timeout_ms)) {
		//bool error = false;
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

	if(got_response) {
		return 0;
	} if(error) {
		return -1;
	} else {
		return 1;
	}


}

//set the time at which the next transaction should happen
//takes the time in microseconds until the next transaction, returns the scheduled timestamp in radio units
uint64_t set_outgoing_time(uint64_t turnaround_time_micros, uint64_t rx_time_radio) {
    
    //convert micros to DWT time and add it to the radio time, then shift it into the delay time register since that one chops off the LSByte
    uint64_t tx_timestamp = ((turnaround_time_micros * UUS_TO_DWT_TIME + rx_time_radio) >> 8) & 0xFFFFFFFEUL;
    radio->dwt_setdelayedtrxtime((uint32_t)tx_timestamp);

    //make units the same as radio->get_rx_timestamp_u64
    return (tx_timestamp << 8) + TX_ANT_DELAY;
}


////////////////////main methods

//shared over all three radios
void setup()
{

	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(BAUD_RATE);
	Serial.println("Begin");


	// sets up the SPI connection to the DW3000 radio
	SPI = SPIClass(NRF_SPI2, SPI_MISO, SPI_CLK, SPI_MOSI);
	SPI.begin();


	//set up the backend components and feed them into the main DW3000 class
	dummy = new DummyStream(); //use this if you want to silence the debug output from the DW3000 library
	uart = new DWUart(*dummy);
	//uart = new DWUart(BAUD_RATE);
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
	
	// uses DWT_LOADUCODE, which we don't have documentation for
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


	while (radio->dwt_configure(&config_ch5))
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
	
	//set these to 0 to start with; we'll be calibrating the radios with this code
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


//sender (sends only)
void loop_0() {

	radio->gpio_set(2, flip);
	flip = !flip;

	//clean slate
	radio->clear_system_status();
	radio->dwt_forcetrxoff();

	//1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);
	//send packet
	char* payload = "";
	auto initial_packet = UWBPacket(get_uuid(), UWBPacket::BROADCAST_MAC, PacketType::UserDefined, (uint8_t*)payload, sizeof(payload));

	send_packet(initial_packet, DWT_START_TX_IMMEDIATE);

	delay(RADIO_0_TX_FREQUENCY_MILLIS);



}

//radio to be calibrated (gets and sends)
void loop_1() {



	//clean slate
	radio->clear_system_status();
	radio->dwt_forcetrxoff();
	
	//1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);

	//start listening
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

	//wait for response
    auto response = 0;
    do {
        response = radio->check_for_rx();
    } while(response == 0);

    //got response
    if(response == 1) {
	
		//send a relayed packet
		auto outgoing_time = set_outgoing_time(RADIO_TURNAROUND_TIME_MICROS, radio->get_rx_timestamp_u64());
		auto clock_drift = radio->dwt_readcarrierintegrator();

		//this is not what this is for, but I'm using it anyway since it can do what I want
		RangingPacket packet = RangingPacket(outgoing_time, clock_drift, RangingFrameNum::Request);

		//format and send packet
		auto relay_1_p = UWBPacket(get_uuid(), UWBPacket::BROADCAST_MAC, PacketType::UserDefined, packet.get_compiled(), packet.get_compiled_len());

		if(send_packet(relay_1_p, DWT_START_TX_DELAYED))
		{
			radio->gpio_set(2, flip);
			flip = !flip;
		}

	}

}


//used to determine the average error over a number of points
double rolling_average_count = 0;
double old_average = 0;

//receiver
void loop_2() {

	const auto clock_offset_ratio_multiplier_ch5 = FREQ_OFFSET_MULTIPLIER*HERTZ_TO_PPM_MULTIPLIER_CHAN_5/1e6;
	const auto clock_offset_ratio_multiplier_ch9 = FREQ_OFFSET_MULTIPLIER*HERTZ_TO_PPM_MULTIPLIER_CHAN_9/1e6;



	//clean slate
	radio->clear_system_status();
	radio->dwt_forcetrxoff();
	
	//1000 microseconds * 1000, should be 1 second
    radio->dwt_setrxtimeout(UUS_TO_DWT_TIME * 1000000u);

	//start listening
	radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);

	//wait for response
    auto response = 0;
    do {
        response = radio->check_for_rx();
    } while(response == 0);

    //got response
    if(response == 1) {

		radio->gpio_set(2, flip);
		flip = !flip;
	
		//collect first data
		auto initial_rx_time = radio->get_rx_timestamp_u64();
		auto initial_ci = radio->dwt_readcarrierintegrator();

		//listen for the second packet
		radio->dwt_rxenable(DWT_START_RX_IMMEDIATE);
		do {
			response = radio->check_for_rx();
		} while(response == 0);


		if(response == 1) {

			//collect second data
			auto second_rx_time = radio->get_rx_timestamp_u64();
			auto second_ci = radio->dwt_readcarrierintegrator();

			//collect first data from other radio
			auto remote_packet = get_packet();
			RangingPacket rp = RangingPacket(remote_packet.get_payload());
			auto remote_rx_time = rp.get_reply_time();
			int32_t remote_ci = (int32_t)(uint32_t)rp.get_round_time();

			//time-of-arrival difference between when we got the packet from radio 0 and from radio 1
			uint64_t rx_difference = second_rx_time - initial_rx_time;

			//get the offset ratio between this radio and the second one
			auto clock_offset_ratio = 1.0 - (clock_offset_ratio_multiplier_ch5 * second_ci);

			
			//calculate the leftover time after all the ranging has been done.
			auto leftover_dw_time = rx_difference - (EXPECTED_TOF * 2) - (RADIO_TURNAROUND_TIME_MICROS * UUS_TO_DWT_TIME * clock_offset_ratio);


			//perform rolling average with the leftover time

			//protect from outliers
			if(abs(old_average - leftover_dw_time) < old_average || rolling_average_count < 0.5) {
				rolling_average_count += 1.0;
				double new_average = old_average + (leftover_dw_time - old_average) / rolling_average_count;
				old_average = new_average;
			}

			//dump to serial (debug: extra data from remotes)
			if(0) {
				//the time when we got the inital message
				print_u64(initial_rx_time, 10);
				Serial.print(",");
				Serial.print(initial_ci);
				Serial.print(",");

				//the time when we got the second message from remote
				print_u64(second_rx_time, 10);
				Serial.print(",");
				Serial.print(second_ci);
				Serial.print(",");

				//the time when remote got the initial message (as part of remote's packet)
				print_u64(remote_rx_time, 10);
				Serial.print(",");
				Serial.print(remote_ci);
				Serial.print(",");

				//outlier check
				print_u64(rx_difference, 16);
				Serial.print(",");
			}

			//average total delay
			Serial.print(leftover_dw_time);
			Serial.print(",");
			Serial.print(old_average);
			Serial.print(",");

			//suggested RX and TX delays
			Serial.print(old_average * 0.56); //rx
			Serial.print(",");
			Serial.print(old_average * 0.44); //tx

			Serial.println("");


		




		}




	}

}

void loop() {

#if RADIO_NUM == 0
	loop_0();
#elif RADIO_NUM == 1
	loop_1();
#else
	loop_2();
#endif
	

}