// MuleSkin-CYD — the seam between the app and the Freenove 3.5"'s ST77922.
//
// The same arrangement as the CrowPanel 7 (crowpanel7_display.h says why at
// length): main.cpp upcasts its off-screen sprite to TFT_eSPI*, so TFT_eSPI
// has to stay as the TYPE, and it stops being a DRIVER. Its constructor is
// inert, the hot drawing methods are virtual, and the sprite never reaches
// the parent's bus except through pushSprite(), which this board never
// calls: FramePush's QSPI route ships the frame instead, through the push
// task, and St77922 (st77922_qspi.h) owns the bus.
//
// Rotation is the app's. The panel cannot swap its axes (its MADCTL has no
// MV), so it is always addressed in portrait, and rotations 1 and 3 are done
// in software -- by FramePush as it converts each chunk, and by toPanel()
// below for the few things that draw straight at the panel. The library's
// own width/height bookkeeping is kept in step so sprites and fonts measure
// the right way round. The convention is TouchFit's (touch_fit.h), so a tap
// lands where it was drawn in all four.
#pragma once
#include <TFT_eSPI.h>

class St77922TFT : public TFT_eSPI {
public:
    St77922TFT() : TFT_eSPI(320, 480) {}

    // ---- hidden, not overridden -------------------------------------------
    // Non-virtual in the base and only ever called by name on the one global
    // `tft`, so name-hiding binds them to ours.
    void init(uint8_t tc = 0);
    void begin(uint8_t tc = 0) { init(tc); }
    void setRotation(uint8_t r);
    // applyColorOrder() in main.cpp sends MADCTL through these on the SPI
    // boards; the panel's MADCTL stays at 0 here (portrait, RGB).
    void writecommand(uint8_t) {}
    void writecommand(uint16_t) {}
    void writedata(uint8_t) {}
    void invertDisplay(bool on);
    // No transactions and no address window from the library's side.
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t, int32_t, int32_t, int32_t) {}
    void fillScreen(uint32_t color);

    // ---- overridden virtuals ----------------------------------------------
    // The net under the whole app: if the sprite ever fails to allocate,
    // main.cpp reseats `canvas` to point at `tft` itself and keeps drawing.
    int16_t  width(void) override  { return _width; }
    int16_t  height(void) override { return _height; }
    void     drawPixel(int32_t x, int32_t y, uint32_t color) override;
    void     fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) override;
    void     drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) override;
    void     drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) override;
    void     drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) override;
    uint16_t readPixel(int32_t x, int32_t y) override;
    void     pushColor(uint16_t color) override;
    void     setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) override;

    // Text, drawn into a small sprite and copied across with fillRect. The
    // library's own text routines write straight to the S3's SPI2 registers
    // -- the peripheral the panel's QSPI driver owns -- and the driver only
    // re-applies its settings when another device has had the bus, so one
    // line of boot text left every frame after it going out wrong: a green
    // cast over everything (found on the board 2026-10-09).
    void     drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) override;
    int16_t  drawChar(uint16_t uniCode, int32_t x, int32_t y) override;
    // The library's write bracket: SPI.beginTransaction() and the chip
    // select, on that same peripheral. Nothing to bracket here.
    void     begin_nin_write() override {}
    void     end_nin_write() override {}

    // A rectangle in rotation `rot`'s screen coordinates to the panel's own
    // portrait frame. Pure arithmetic; FramePush uses the same mapping.
    static void toPanel(uint8_t rot, int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t& px, int32_t& py, int32_t& pw, int32_t& ph);
private:
    int32_t _wx0 = 0, _wy0 = 0, _wx1 = 0, _wy1 = 0, _wcx = 0, _wcy = 0;
};

// The frame to the panel, from the loop: changed bands only, rotated and
// converted on the way (st77922_display.cpp). MuleSkin's stand-in for
// upstream's QSPI push task.
namespace St77922Blit {
bool push(const uint8_t* src, int32_t w, int32_t h, uint8_t rot);
void invalidate();
}
