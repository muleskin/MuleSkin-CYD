// MuleSkin-CYD — the ST77922 over QSPI. See st77922_qspi.h.
#include "st77922_qspi.h"
#if defined(FREENOVE_S3_35)
#include <Arduino.h>
#include <string.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

namespace St77922 {

#ifndef SQW_QSPI_HZ
#define SQW_QSPI_HZ 80000000
#endif
static const uint32_t QSPI_HZ = SQW_QSPI_HZ;   // Freenove's own rate for this glass is 80 MHz
static const uint8_t  CMD_1W = 0x02, CMD_4W = 0x32;
static const uint8_t  RAM_WRITE = 0x2C, RAM_CONTINUE = 0x3C;

static spi_device_handle_t   s_dev = nullptr;
static bool                  s_ready = false;
static bool                  s_inFlight = false;
static spi_transaction_ext_t s_tx;              // the one pixel transfer in flight
static uint16_t*             s_fill = nullptr;  // a DMA-capable run of one colour, for fillRect
static const size_t          FILL_PX = 2048;

// Freenove's initialisation sequence for this glass (ST77922.cpp), as a
// stream: register, byte count, milliseconds to wait after, the bytes. Kept
// byte for byte; most of it is panel timing and gamma nobody here can
// improve on, and the tail is the part that matters: inversion on (0x21),
// sleep out (0x11), display on (0x29), pixel format (0x3A), MADCTL 0 (0x36:
// portrait, RGB) and tearing-effect output on (0x35).
static const uint8_t INIT[] = {
    0xF1, 1, 0,  0x00,
    0x60, 3, 0,  0x00, 0x00, 0x00,
    0x65, 1, 0,  0x80,
    0x79, 1, 0,  0x06,
    0x7B, 3, 0,  0x00, 0x08, 0x08,
    0x80, 11, 0, 0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA,
    0x81, 4, 0,  0x26, 0x52, 0x72, 0x27,
    0x84, 2, 0,  0x92, 0x25,
    0x87, 6, 0,  0x10, 0x10, 0x58, 0x00, 0x02, 0x3A,
    0x88, 15, 0, 0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06,
    0x89, 3, 0,  0x00, 0x00, 0x00,
    0x8A, 11, 0, 0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19,
    0x8B, 9, 0,  0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E,
    0x8C, 13, 0, 0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09,
    0x8D, 1, 0,  0x0C,
    0x8E, 6, 0,  0x33, 0x01, 0x0C, 0x13, 0x01, 0x01,
    0xB3, 2, 0,  0x00, 0x30,
    0xF1, 1, 0,  0x00,
    0x71, 1, 0,  0xD0,
    0x66, 2, 0,  0x02, 0x3F,
    0xBE, 3, 0,  0x26, 0x00, 0x9D,
    0x70, 12, 0, 0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A,
    0x90, 9, 0,  0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27,
    0x91, 9, 0,  0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27,
    0x92, 10, 0, 0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27,
    0x93, 10, 0, 0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27,
    0x94, 6, 0,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x95, 5, 0,  0x96, 0x16, 0x00, 0x00, 0xFF,
    0x96, 12, 0, 0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44,
    0x97, 12, 0, 0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44,
    0xBA, 5, 0,  0x55, 0x94, 0x2D, 0x94, 0x27,
    0x9A, 7, 0,  0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9B, 7, 0,  0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9C, 13, 0, 0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00,
    0x9D, 8, 0,  0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0x9E, 7, 0,  0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0xB4, 12, 0, 0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10,
    0xB5, 12, 0, 0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12,
    0xB6, 7, 0,  0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF,
    0x86, 14, 0, 0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5,
    0xB7, 16, 0, 0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D,
    0xB8, 16, 0, 0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D,
    0xB9, 2, 0,  0x23, 0x23,
    0xBF, 6, 0,  0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B,
    0xF2, 1, 0,  0x00,
    0x73, 5, 0,  0x04, 0xDA, 0x12, 0x54, 0x47,
    0x77, 5, 0,  0x6B, 0x5B, 0xFD, 0xC3, 0xC5,
    0x7A, 2, 0,  0x15, 0x27,
    0x7B, 2, 0,  0x04, 0x57,
    0x7E, 2, 0,  0x01, 0x0E,
    0xBF, 1, 0,  0x36,
    0xE3, 2, 0,  0x40, 0x40,
    0xF0, 1, 0,  0x00,
    0xD0, 1, 0,  0x00,
    0x2A, 4, 0,  0x00, 0x00, 0x01, 0x3F,
    0x2B, 4, 0,  0x00, 0x00, 0x01, 0xDF,
    0x21, 0, 0,
    0x11, 0, 120,
    0x29, 0, 0,
    0x2C, 0, 0,
    0x3A, 1, 0,  0x01,
    0x36, 1, 0,  0x00,
    0x35, 1, 20, 0x01,
};

void writeReg(uint8_t reg, const uint8_t* data, size_t len);
bool ready() { return s_ready; }

void reinit() {
    if (!s_ready) return;
    for (size_t i = 0; i < sizeof INIT;) {
        const uint8_t reg = INIT[i], n = INIT[i + 1], ms = INIT[i + 2];
        writeReg(reg, INIT + i + 3, n);
        if (ms) delay(ms);
        i += 3 + n;
    }
}

// A one-lane command: `reg` in the address, `len` bytes after. Synchronous.
void writeReg(uint8_t reg, const uint8_t* data, size_t len) {
    if (!s_ready) return;
    wait();
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof t);
    t.base.flags     = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    t.base.cmd       = CMD_1W;
    t.base.addr      = (uint32_t)reg << 8;
    t.command_bits   = 8;
    t.address_bits   = 24;
    if (len) {
        // Small, and the driver copies anything under four words into its
        // own DMA-safe space; INIT lives in flash, which DMA cannot read.
        uint8_t tmp[16];
        if (len > sizeof tmp) len = sizeof tmp;
        memcpy(tmp, data, len);
        t.base.tx_buffer = tmp;
        t.base.length    = 8 * len;
        spi_device_polling_transmit(s_dev, &t.base);
    } else {
        spi_device_polling_transmit(s_dev, &t.base);
    }
}

bool begin() {
    if (s_ready) return true;
    spi_bus_config_t bus;
    memset(&bus, 0, sizeof bus);
    bus.mosi_io_num     = IO_D0;
    bus.miso_io_num     = IO_D1;
    bus.sclk_io_num     = IO_SCLK;
    bus.quadwp_io_num   = IO_D2;
    bus.quadhd_io_num   = IO_D3;
    bus.data4_io_num    = -1;
    bus.data5_io_num    = -1;
    bus.data6_io_num    = -1;
    bus.data7_io_num    = -1;
    bus.max_transfer_sz = (int)(MAX_PIXELS * 2 + 8);
    bus.flags           = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_IOMUX_PINS | SPICOMMON_BUSFLAG_QUAD;
    esp_err_t r = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) { Serial.printf("[st77922] bus would not start: %d\n", (int)r); return false; }
    spi_device_interface_config_t dev;
    memset(&dev, 0, sizeof dev);
    dev.command_bits   = 0;      // set per transaction
    dev.address_bits   = 0;
    dev.mode           = 0;
    dev.clock_speed_hz = (int)QSPI_HZ;
    dev.spics_io_num   = IO_CS;
    dev.flags          = SPI_DEVICE_HALFDUPLEX;
    dev.queue_size     = 4;
    r = spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
    if (r != ESP_OK) { Serial.printf("[st77922] device would not add: %d\n", (int)r); return false; }
    s_fill = (uint16_t*)heap_caps_malloc(FILL_PX * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_ready = true;
    reinit();
    Serial.printf("[st77922] up: 320x480 over QSPI at %lu MHz, fill run %s\n",
                  (unsigned long)(QSPI_HZ / 1000000), s_fill ? "ready" : "NOT allocated");
#if defined(SQW_QSPI_TEST)
    // Bring-up only (-DSQW_QSPI_TEST=1): four quadrants in the panel's own
    // frame, three seconds, before anything else draws. Top-left red,
    // top-right green, bottom-left blue, bottom-right white, with the
    // backlight forced on. Says whether the direct path puts the right
    // colour in the right place, with no frame push in the picture.
    pinMode(IO_BL, OUTPUT); digitalWrite(IO_BL, HIGH);
    fillRect(0,   0,   160, 240, 0xF800);
    fillRect(160, 0,   160, 240, 0x07E0);
    fillRect(0,   240, 160, 240, 0x001F);
    fillRect(160, 240, 160, 240, 0xFFFF);
    Serial.println("[st77922] TEST: red | green over blue | white, portrait, for 3 s");
    delay(3000);
#endif
    return true;
}

void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    const uint8_t xs[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    const uint8_t ys[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    writeReg(0x2A, xs, 4);
    writeReg(0x2B, ys, 4);
}

void wait() {
    if (!s_inFlight) return;
    spi_transaction_t* done = nullptr;
    spi_device_get_trans_result(s_dev, &done, portMAX_DELAY);
    s_inFlight = false;
}

bool queuePixels(const uint16_t* dma, size_t count, bool first) {
    if (!s_ready || !count) return false;
    if (count > MAX_PIXELS) count = MAX_PIXELS;
    // Freenove's driver never sends pixels behind 0x2C itself: memory write
    // is opened with a bare 0x2C on one lane, and every run of pixels after
    // it goes behind 0x3C, continue. Done the same way here.
    if (first) writeReg(RAM_WRITE, nullptr, 0);
    wait();
    memset(&s_tx, 0, sizeof s_tx);
    s_tx.base.flags     = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    s_tx.base.cmd       = CMD_4W;
    s_tx.base.addr      = (uint32_t)RAM_CONTINUE << 8;
    s_tx.command_bits   = 8;
    s_tx.address_bits   = 24;
    s_tx.base.tx_buffer = dma;
    s_tx.base.length    = count * 16;
    const esp_err_t r = spi_device_queue_trans(s_dev, &s_tx.base, portMAX_DELAY);
    if (r != ESP_OK) { Serial.printf("[st77922] queue failed: %d\n", (int)r); return false; }
    s_inFlight = true;
    return true;
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565) {
    if (!s_ready || !s_fill || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > PANEL_W) w = PANEL_W - x;
    if (y + h > PANEL_H) h = PANEL_H - y;
    if (w <= 0 || h <= 0) return;
    const uint16_t c = wire(rgb565);
    for (size_t i = 0; i < FILL_PX; i++) s_fill[i] = c;
    setWindow(x, y, x + w - 1, y + h - 1);
    size_t left = (size_t)w * (size_t)h;
    bool first = true;
    while (left) {
        const size_t n = left < FILL_PX ? left : FILL_PX;
        queuePixels(s_fill, n, first);
        first = false;
        left -= n;
    }
    wait();
}

void invert(bool on) { writeReg(on ? 0x21 : 0x20, nullptr, 0); }


}  // namespace St77922
#endif  // FREENOVE_S3_35
