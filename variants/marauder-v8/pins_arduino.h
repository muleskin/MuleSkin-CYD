// Arduino pin names for justcallmekoko's Marauder V8 (ESP32-C5, 8 MB).
//
// Only the chip's own pins. The screen, touch and SD pins live in
// include/marauder_v8_user_setup.h and nowhere else: the NM-CYD-C5's variant,
// borrowed at first, also defines a whole TFT_eSPI setup (ST7789, backlight 25,
// touch CS 1) that quietly overrode the V8's -- the screen stayed dark with
// the backlight driven on the wrong pin (2026-10-12).
#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

static const uint8_t TX = 11;
static const uint8_t RX = 12;

static const uint8_t USB_DM = 13;
static const uint8_t USB_DP = 14;

// The board's I2C: the MAX17048 battery gauge, SCL 4 and SDA 5 (Marauder's
// configs.h, I2C_SCL 4 / I2C_SDA 5).
static const uint8_t SDA = 5;
static const uint8_t SCL = 4;

// The shared SPI bus: screen, touch and SD card.
static const uint8_t SS   = 10;   // the SD card's chip select
static const uint8_t MOSI = 7;
static const uint8_t MISO = 2;
static const uint8_t SCK  = 6;

static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;

// LP I2C and LP UART are fixed on the ESP32-C5.
static const uint8_t LP_SDA = 4;
static const uint8_t LP_SCL = 5;
#define WIRE1_PIN_DEFINED
#define SDA1 LP_SDA
#define SCL1 LP_SCL
static const uint8_t LP_RX = 12;
static const uint8_t LP_TX = 11;

#endif /* Pins_Arduino_h */
