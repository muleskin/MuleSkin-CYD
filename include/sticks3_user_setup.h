// MuleSkin-CYD -- TFT_eSPI user setup for the M5Stack StickS3.
//
// ESP32-S3-PICO-1-N8R8 (8 MB flash, 8 MB OPI PSRAM), a 1.14" 135x240
// ST7789P3 on its own SPI, no touch at all, two buttons (GPIO11, GPIO12), and
// an M5PM1 power chip on I2C (SDA 47, SCL 48, 0x6E) that has to switch the
// panel's rail on before it answers -- see sticksPowerUp() in main.cpp. Pins
// from M5Stack's StickS3 page and M5GFX's autodetect for the board.
#pragma once

#define USER_SETUP_INFO    "MuleSkin-CYD / M5Stack StickS3 / ST7789 135x240"
#define ST7789_DRIVER
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
// The 135x240 glass sits in the middle of the controller's 240x320 memory:
// TFT_eSPI knows the 52/40 offsets for this size once it is told to use them.
#define CGRAM_OFFSET

#define TFT_MISO  -1
#define TFT_MOSI  39
#define TFT_SCLK  40
#define TFT_CS    41
#define TFT_DC    45
#define TFT_RST   21
// GPIO38 is the backlight. TFT_eSPI is not given it: main.cpp runs the
// LEDC dimmer on it the way it does on every other board.

#define TFT_INVERSION_ON     // M5GFX sets invert for this panel

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
