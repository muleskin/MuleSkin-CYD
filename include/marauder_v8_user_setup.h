// TFT_eSPI setup for justcallmekoko's Marauder V8: an ESP32-C5, a 2.8" 240x320
// ILI9341 with an XPT2046 resistive touch chip and the SD card on the same SPI
// bus. Marauder's own V8 setup is not public, so the pins were found on the
// board (2026-10-12, sim/out/marauder_v8/probe): every arrangement of the free
// GPIOs was asked for the ILI9341's ID (93 41, read the way TFT_eSPI does) and
// the XPT2046's temperature, and one of each answered. They are the NM-CYD-C5's
// bus exactly -- the two boards share a design lineage -- with the touch chip
// select on 9 rather than 1. SD chip select is 10 (Marauder's configs.h), the
// same as the NM-CYD-C5, so sd_log.cpp's NM_CYD_C5 branch serves both.
//
// Backlight: GPIO8, the one free pin that read low against the chip's own
// pull-up while everything else read high -- a pull-down on a transistor's
// gate, which is how a backlight switch sits. CONFIRMED 2026-10-12: driven
// HIGH it lights the screen (sim/out/marauder_v8/bltest, one candidate at a
// time, seen by eye).
// Reset is not on a free GPIO (the panel answered with nothing driving one),
// so it is the board's: -1.
#define USER_SETUP_INFO "MuleSkin-CYD / Marauder V8 / ILI9341"

#define ILI9341_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320
#define TFT_INVERSION_OFF
// BGR: with RGB the colours came out swapped (seen 2026-10-12).
#define TFT_RGB_ORDER TFT_BGR

#define TFT_BL   8
#define TFT_BACKLIGHT_ON HIGH

#define TFT_MISO  2
#define TFT_MOSI  7
#define TFT_SCLK  6
#define TFT_CS    23
#define TFT_DC    24
#define TFT_RST   -1

#define TOUCH_CS  9

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY  20000000
#endif
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
