// MuleSkin-CYD — TFT_eSPI setup for the Freenove ESP32-S3 Display 4.0"
// (FNK0104S, capacitive touch).
//
// Pulled in via -include in platformio.ini's [env:freenove-s3-40], ahead of
// TFT_eSPI's own User_Setup, so nothing in the library tree is edited.
//
// The 2.8"'s board (freenove_s3_user_setup.h) with a bigger panel: a 320x480
// ST7796 on the same SPI pins. Pins from Freenove's own TFT_eSPI setup for
// this board (Libraries/FNK0104S/TFT_eSPI_Setups_v1.2.zip,
// FNK0104S_4.0_320x480_ST7796.h): display on 11/12, CS 10, DC 46, backlight
// 45 (HIGH is on), no reset line, and no MISO -- Freenove leave it undefined
// here, where the 2.8" has it on 13. Inversion ON, as Freenove ship it; the
// control point is main.cpp's PANEL_NEEDS_INVERSION.
//
// Everything off this bus is the 2.8"'s too, per Freenove's sketches, which
// only ever split the 3.5" out: FT6336 touch on I2C (SDA 16, SCL 15, reset
// 18), SDMMC on 38-41/47/48, WS2812 on 42, battery on 9. So the build is
// FREENOVE_S3 plus FREENOVE_S3_40 for what the panel's size changes.
#pragma once

#define USER_SETUP_INFO    "MuleSkin-CYD / Freenove ESP32-S3 4.0 inch / ST7796"

#define ST7796_DRIVER
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

#define USE_HSPI_PORT
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_CS    10
#define TFT_DC    46
#define TFT_RST   -1   // not wired; the panel comes up with power
#define TFT_BL    45
#define TFT_BACKLIGHT_ON HIGH

// The same fonts every other CYD build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// Freenove's own rate for this panel. If it shows torn or speckled rows,
// build with -DSPI_FREQUENCY=40000000.
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         80000000
#endif
#define SPI_READ_FREQUENCY    20000000

// RGB, not the 2.8"'s BGR: on an FNK0104S the colour check came up with red
// and blue swapped under BGR and needed ORDER tapped once (2026-10-12).
#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_RGB
