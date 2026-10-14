// MuleSkin-CYD -- TFT_eSPI user setup for the M5Stack Cardputer ADV.
//
// ESP32-S3FN8 (Stamp-S3A: 8 MB flash, NO PSRAM), the same 1.14" 135x240
// ST7789 as the StickS3, no touch, a 56-key keyboard behind a TCA8418 on I2C
// (SDA 8, SCL 9, INT 11), microSD on CS 12 / MOSI 14 / SCK 40 / MISO 39, and
// the cell on GPIO10 through a divider. Pins from M5Stack's Cardputer-Adv
// page and M5GFX's autodetect.
#pragma once

#define USER_SETUP_INFO    "MuleSkin-CYD / M5Stack Cardputer ADV / ST7789 135x240"
#define ST7789_DRIVER
#define TFT_WIDTH   135
#define TFT_HEIGHT  240
#define CGRAM_OFFSET         // the 52/40 offsets of a 135x240 glass

#define TFT_MISO  -1
#define TFT_MOSI  35
#define TFT_SCLK  36
#define TFT_CS    37
#define TFT_DC    34
#define TFT_RST   33
// GPIO38 is the backlight, run by main.cpp's LEDC dimmer.

#define TFT_INVERSION_ON

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    16000000

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT
