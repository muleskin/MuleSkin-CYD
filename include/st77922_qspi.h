// MuleSkin-CYD — the Freenove ESP32-S3 Display 3.5"'s panel: a Sitronix
// ST77922, 320x480, on a four-lane QSPI bus.
//
// Not a TFT_eSPI driver. The library drives nothing on this board (see
// freenove_s3_35_user_setup.h); this module owns the bus and the panel, and
// two callers use it: St77922TFT (st77922_display.h) for the few things that
// draw straight at the panel, and St77922Blit (st77922_display.cpp), which
// ships the frame a chunk at a time by DMA from the loop.
//
// The wire protocol, from Freenove's own driver (Libraries/FNK0104N,
// ST77922.cpp) and the ST77922 spec: a register write is command 0x02 on one
// lane with the register in the top byte of a 24-bit address, then its bytes
// on one lane; pixels are command 0x32 with 0x2C (memory write) or 0x3C
// (continue) in that address, then the data on all four lanes, 16 bits a
// pixel, high byte first. The panel's MADCTL cannot swap its axes, so it is
// always addressed in portrait (320 wide, 480 tall) and the rotation the app
// asked for is done by whoever feeds it -- see St77922TFT::toPanel().
//
// Only ever called from the loop task: MuleSkin pushes its frames there.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace St77922 {

static const int16_t PANEL_W = 320;   // the panel's own frame, portrait
static const int16_t PANEL_H = 480;

// Pins, from Freenove's ST77922.h and the 3.5" schematic: the S3's FSPI
// IO_MUX pins, which is what lets the bus run at 80 MHz. (IO_ rather than
// PIN_: TFT_eSPI's setup header owns PIN_D0 and friends.)
static const int IO_SCLK = 12, IO_D0 = 11, IO_D1 = 13, IO_D2 = 14, IO_D3 = 9;
static const int IO_CS   = 10;
static const int IO_BL   = 41;       // through a BSS138: HIGH is on

// The largest single transfer, in pixels: under the driver's 64 KB DMA cap.
static const size_t MAX_PIXELS = 0x4000;

// The bus and the panel's initialisation sequence. The backlight is left to
// main.cpp's PWM. False if the SPI driver would not start, in which case
// every call below does nothing and the screen stays dark.
bool begin();
bool ready();

// The initialisation sequence again. The ST77922 is a TDDI part, touch and
// display one chip, so the touch's reset line may well reset the display
// too; main.cpp runs this after pulsing it, as a precaution. (It was NOT
// the green cast seen in bring-up -- that was the library's own text
// routines writing to this bus; see St77922TFT::drawChar.)
void reinit();

// A register write: `reg`, then `len` bytes (0 for none).
void writeReg(uint8_t reg, const uint8_t* data, size_t len);

// The write window, in PANEL pixels, inclusive corners.
void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// `count` pixels in wire order (high byte first in memory) from a
// DMA-capable buffer, into the window. `first` starts the write at the
// window's origin; false carries on from where the last one stopped. The
// transfer is queued and this returns at once, so the caller can convert
// the next chunk while this one is on the wire; the next call, or wait(),
// blocks until it has gone. At most one is ever in flight, so a buffer
// handed in here is free again once the call after it returns.
bool queuePixels(const uint16_t* dma, size_t count, bool first);
void wait();

// A solid rectangle in PANEL pixels, synchronously: the direct draws.
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t rgb565);

// Display inversion on or off: the panel's 0x21 / 0x20.
void invert(bool on);


// One RGB565 colour as the panel wants it in memory: high byte first.
inline uint16_t wire(uint16_t rgb565) { return (uint16_t)((rgb565 << 8) | (rgb565 >> 8)); }

}  // namespace St77922
