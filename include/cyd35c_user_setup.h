// MuleSkin-CYD — TFT_eSPI user setup for the 3.5" Sunton CYD with
// CAPACITIVE touch (ESP32-3248S035C): the same ST7796 panel and pins as the
// resistive one (cyd35_user_setup.h), with no TOUCH_CS -- touch is a GT911 on
// I2C (SDA 33, SCL 32, reset 25, INT 21), driven by gt911_touch.cpp, so
// TFT_eSPI's resistive touch must not claim a pin. Contributed upstream in SquachWatch PR #24 (VV-B0Y).
#pragma once

#define USER_SETUP_INFO    "MuleSkin-CYD / 3.5 inch / ST7796 / GT911"

#define ST7796_DRIVER

// Portrait native (320 wide x 480 tall glass), rotated to landscape at
// runtime via setRotation(1), same as the resistive 3.5".
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1   // Tied to EN on this board -- no software reset pin.
#define TFT_BL    27

#define TFT_BACKLIGHT_ON   1
#define PWM_FREQ           5000
#define PWM_MAX_DUTY       255

#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         40000000
#endif
#define SPI_READ_FREQUENCY    20000000

#define LOAD_GLCD
#define LOAD_FONT2

#define TFT_INVERSION_ON
#define TFT_RGB_ORDER TFT_BGR
