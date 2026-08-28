# DW3000 Arduino library

A modular DW3000 library based on the original [work done by Makerfabs](https://github.com/Makerfabs/Makerfabs-ESP32-UWB-DW3000).

## What is this fork?

The fork encapsulates all of the sub-library methods in classes instead of raw C functions in order to keep everything a little bit more organized and modular. (and, for instance, instantiate more than one UWB chip per microcontroller, if, for some reason, you need to do that...)

It also removes or replaces some of the low-level platform-specific function calls with arduino generics to make everything more portable.


## Hardware

Currently, I'm doing development with the DWM3001CDK, which integrates a DW3000 chip and NRF52833 chip onto the same package, as well as an integrated J-Link debugger for uploading and breakpoint debugging on-chip.

I have to pair the chip with a usb-ttl adapter to get serial messages out of it since even though it has USB-CDC support in hardware, the available arduino core for this chip does not implement it.

The pins that connect to the DW3000 are as follows (these can also be found in the constants.h file in the example folders):
```
#define SPI_CS 32 + 6
#define SPI_CLK 3
#define SPI_MOSI 8
#define SPI_MISO 29

#define DW_RST 25
#define DW_IRQ 32 + 2
#define DW_WUP 32 + 19
```



