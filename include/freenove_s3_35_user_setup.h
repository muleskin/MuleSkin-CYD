// MuleSkin-CYD — TFT_eSPI "user setup" for the Freenove ESP32-S3 Display
// 3.5" (FNK0104N), which describes a display that does not exist.
//
// The panel is an ST77922 on a four-lane (QSPI) bus, which TFT_eSPI cannot
// drive. As on the CrowPanel 7, TFT_eSPI stays as a TYPE only: the app's
// sprite derives from it, and St77922TFT (st77922_display.h) hides every
// method that would reach a bus. st77922_qspi.cpp drives the panel through
// the IDF's SPI master in quad mode, and FramePush's QSPI route ships each
// frame (frame_push.h, SQW_PUSH_QSPI). tft.init() never reaches the library,
// so none of the pins below are ever touched.
//
// Pulled in via -include in platformio.ini's [env:freenove-s3-35].
#pragma once

#define USER_SETUP_INFO "MuleSkin-CYD / Freenove ESP32-S3 3.5 inch / ST77922 over QSPI"

// A driver has to be named for the library to compile; nothing is sent to it.
#define ST7789_DRIVER
#define TFT_WIDTH   320
#define TFT_HEIGHT  480

#define TFT_MISO  -1
#define TFT_MOSI  -1
#define TFT_SCLK  -1
#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1

// The same fonts every other CYD build loads, and no more.
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SMOOTH_FONT

// Unused: the library never opens a bus here. The panel's own clock is
// St77922's QSPI_HZ (st77922_qspi.cpp).
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY         20000000
#endif
#define SPI_READ_FREQUENCY    20000000
