// MuleSkin-CYD — St77922TFT. See st77922_display.h.
#include "st77922_display.h"
#if defined(FREENOVE_S3_35)
#include "st77922_qspi.h"
#include "frame_push.h"   // rgb332Wire, frameSpans: the same colour and spans as every board
#include <esp_heap_caps.h>

void St77922TFT::init(uint8_t) {
    // Deliberately does NOT call TFT_eSPI::init(): that would emit an ST7789
    // initialisation sequence down a bus this board does not have. The real
    // panel comes up here instead.
    St77922::begin();
    _init_width  = _width  = St77922::PANEL_W;
    _init_height = _height = St77922::PANEL_H;
    rotation = 0;
    resetViewport();
}

void St77922TFT::setRotation(uint8_t r) {
    rotation = (uint8_t)(r & 3);
    if (rotation & 1) { _width = St77922::PANEL_H; _height = St77922::PANEL_W; }
    else              { _width = St77922::PANEL_W; _height = St77922::PANEL_H; }
    resetViewport();
    // Whatever the panel shows is in the old orientation now.
    St77922Blit::invalidate();
}

void St77922TFT::invertDisplay(bool on) {
    St77922::invert(on);
}

// touch_fit.h's screenToNative, for whole pixels: rotation 1 puts screen x
// down the panel and screen y across it from the right.
void St77922TFT::toPanel(uint8_t rot, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t& px, int32_t& py, int32_t& pw, int32_t& ph) {
    const int32_t W = St77922::PANEL_W, H = St77922::PANEL_H;
    switch (rot & 3) {
        case 0:  px = x;         py = y;         pw = w; ph = h; break;
        case 1:  px = W - y - h; py = x;         pw = h; ph = w; break;
        case 2:  px = W - x - w; py = H - y - h; pw = w; ph = h; break;
        default: px = y;         py = H - x - w; pw = h; ph = w; break;
    }
}

void St77922TFT::fillScreen(uint32_t color) { fillRect(0, 0, _width, _height, color); }

void St77922TFT::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > _width)  w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w <= 0 || h <= 0) return;
    int32_t px, py, pw, ph;
    toPanel(rotation, x, y, w, h, px, py, pw, ph);
    // (Upstream locks the bus here against its push task; MuleSkin pushes
    // the frame from the loop, the same task as this, so there is no race.)
    St77922::fillRect((int16_t)px, (int16_t)py, (int16_t)pw, (int16_t)ph, (uint16_t)color);
    St77922Blit::invalidate();   // the panel now holds something the push did not record
}

void St77922TFT::drawPixel(int32_t x, int32_t y, uint32_t color) { fillRect(x, y, 1, 1, color); }
void St77922TFT::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) { fillRect(x, y, w, 1, color); }
void St77922TFT::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) { fillRect(x, y, 1, h, color); }

void St77922TFT::drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t color) {
    // Bresenham through our own drawPixel; the base's version writes through
    // the bus rather than the primitives, so it cannot be inherited.
    int32_t dx = abs(xe - xs), sx = xs < xe ? 1 : -1;
    int32_t dy = -abs(ye - ys), sy = ys < ye ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        drawPixel(xs, ys, color);
        if (xs == xe && ys == ye) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; xs += sx; }
        if (e2 <= dx) { err += dx; ys += sy; }
    }
}

uint16_t St77922TFT::readPixel(int32_t, int32_t) {
    // No read-back on this path, and it is only the net under a failed
    // sprite allocation; the sprite answers readPixel from its own buffer.
    return 0;
}

// A rendered sprite onto the panel, one run of one colour at a time. With
// `transparent`, pixels of the key colour are left alone, which is how the
// library draws text whose background matches its foreground.
static void blitRuns(St77922TFT& t, TFT_eSprite& s, int32_t x0, int32_t y0, int32_t w, int32_t h,
                     bool transparent, uint16_t key) {
    for (int32_t y = 0; y < h; y++) {
        int32_t x = 0;
        while (x < w) {
            const uint16_t c = s.readPixel(x, y);
            int32_t run = 1;
            while (x + run < w && s.readPixel(x + run, y) == c) run++;
            if (!(transparent && c == key)) t.fillRect(x0 + x, y0 + y, run, 1, c);
            x += run;
        }
    }
}

void St77922TFT::drawChar(int32_t x, int32_t y, uint16_t c, uint32_t color, uint32_t bg, uint8_t size) {
    if (size < 1) size = 1;
    const int32_t w = 6 * size, h = 8 * size;
    const bool transparent = (color == bg);
    const uint16_t key = (uint16_t)~color;
    TFT_eSprite s(this);
    s.setColorDepth(16);
    if (!s.createSprite(w, h)) return;
    s.fillSprite(transparent ? key : (uint16_t)bg);
    s.drawChar(0, 0, c, color, transparent ? key : bg, size);
    blitRuns(*this, s, x, y, w, h, transparent, key);
    s.deleteSprite();
}

int16_t St77922TFT::drawChar(uint16_t uniCode, int32_t x, int32_t y, uint8_t font) {
    if (font <= 1) {
        drawChar(x, y, uniCode, textcolor, textbgcolor, textsize);
        return (int16_t)(6 * textsize);
    }
    char str[2] = { (char)uniCode, 0 };
    const int32_t w = textWidth(str, font), h = fontHeight(font);
    if (w <= 0 || h <= 0) return 0;
    const bool transparent = (textcolor == textbgcolor);
    const uint16_t key = (uint16_t)~textcolor;
    TFT_eSprite s(this);
    s.setColorDepth(16);
    if (!s.createSprite(w, h)) return (int16_t)w;
    s.fillSprite(transparent ? key : (uint16_t)textbgcolor);
    s.setTextSize(textsize);
    s.setTextColor(textcolor, transparent ? key : textbgcolor);
    s.drawChar(uniCode, 0, 0, font);
    blitRuns(*this, s, x, y, w, h, transparent, key);
    s.deleteSprite();
    return (int16_t)w;
}

int16_t St77922TFT::drawChar(uint16_t uniCode, int32_t x, int32_t y) {
    return drawChar(uniCode, x, y, textfont);
}

void St77922TFT::setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) {
    _wx0 = _wcx = xs; _wy0 = _wcy = ys; _wx1 = xe; _wy1 = ye;
}

void St77922TFT::pushColor(uint16_t color) {
    drawPixel(_wcx, _wcy, color);
    if (++_wcx > _wx1) { _wcx = _wx0; if (++_wcy > _wy1) _wcy = _wy0; }
}


// ---- St77922Blit: the frame to the panel, from the loop -------------------
// Upstream ships the frame from a push task of its own (frame_push.cpp's
// QSPI route); MuleSkin pushes every board's frame from the loop, so this is
// that route run synchronously: hash each logical row, send only the bands
// that changed (whole, every 64th push), converting 8-bit colour to the
// panel's 16 bits and rotating as it goes. Chunks of 32 panel rows in two
// DMA bounce buffers, so one converts while the other is on the wire.
namespace St77922Blit {
static const int32_t CHUNK = 32;
static uint16_t* s_bounce[2] = { nullptr, nullptr };
static uint16_t  s_lut[256];
static int8_t    s_state = 0;          // 0 untried, 1 ready, -1 unavailable
static bool      s_valid = false;
static int32_t   s_validW = 0, s_validH = 0;
static uint8_t   s_validRot = 0;
static uint32_t  s_pushes = 0;
static uint32_t  s_rowHash[St77922::PANEL_H];   // the longer side: room for any rotation

void invalidate() { s_valid = false; }

static bool ready() {
    if (s_state) return s_state > 0;
    s_state = -1;
    if (!St77922::ready()) { Serial.println("[push] QSPI: the panel never came up"); return false; }
    for (int k = 0; k < 2; k++)
        s_bounce[k] = (uint16_t*)heap_caps_malloc((size_t)St77922::PANEL_W * CHUNK * sizeof(uint16_t),
                                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_bounce[0] || !s_bounce[1]) { Serial.println("[push] QSPI: no room for the bounce buffers"); return false; }
    // 8-bit colour as the panel's 16 bits, high byte first in memory: the
    // same arithmetic as every other board's push (frame_push.h).
    for (int c = 0; c < 256; c++) s_lut[c] = FramePush::rgb332Wire((uint8_t)c);
    s_state = 1;
    return true;
}

static uint32_t rowHash(const uint8_t* p, int32_t n) {
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

// Logical rows [r0, r1) of a w-wide frame in rotation `rot`, to the panel
// (upstream's pushSpanQspi, as it is).
static void span(const uint8_t* src, int32_t w, uint8_t rot, int32_t r0, int32_t r1) {
    const int32_t W = St77922::PANEL_W, H = St77922::PANEL_H;
    int32_t px0, py0, px1, py1;
    switch (rot & 3) {
        case 0:  px0 = 0;      px1 = w - 1;      py0 = r0;     py1 = r1 - 1;     break;
        case 1:  px0 = W - r1; px1 = W - 1 - r0; py0 = 0;      py1 = w - 1;      break;
        case 2:  px0 = 0;      px1 = w - 1;      py0 = H - r1; py1 = H - 1 - r0; break;
        default: px0 = r0;     px1 = r1 - 1;     py0 = 0;      py1 = w - 1;      break;
    }
    St77922::setWindow((int16_t)px0, (int16_t)py0, (int16_t)px1, (int16_t)py1);
    const int32_t wOut = px1 - px0 + 1;
    bool first = true;
    int  k = 0;
    for (int32_t py = py0; py <= py1; py += CHUNK, k++) {
        const int32_t left = py1 - py + 1;
        const int32_t n = left < CHUNK ? left : CHUNK;
        uint16_t* b = s_bounce[k & 1];
        switch (rot & 3) {
            case 0:
                for (int32_t i = 0; i < n; i++) {
                    const uint8_t* s = src + (size_t)(py + i) * (size_t)w;
                    uint16_t* d = b + i * wOut;
                    for (int32_t x = 0; x < wOut; x++) d[x] = s_lut[s[x]];
                }
                break;
            case 2:
                for (int32_t i = 0; i < n; i++) {
                    const uint8_t* s = src + (size_t)(H - 1 - (py + i)) * (size_t)w;
                    uint16_t* d = b + i * wOut;
                    for (int32_t x = 0; x < wOut; x++) d[x] = s_lut[s[w - 1 - x]];
                }
                break;
            case 1:
                for (int32_t ly = r0; ly < r1; ly++) {
                    const uint8_t* s = src + (size_t)ly * (size_t)w + py;
                    uint16_t* d = b + ((W - 1 - ly) - px0);
                    for (int32_t i = 0; i < n; i++) d[i * wOut] = s_lut[s[i]];
                }
                break;
            default:
                for (int32_t ly = r0; ly < r1; ly++) {
                    const uint8_t* s = src + (size_t)ly * (size_t)w + (w - 1 - py);
                    uint16_t* d = b + (ly - px0);
                    for (int32_t i = 0; i < n; i++) d[i * wOut] = s_lut[s[-i]];
                }
                break;
        }
        St77922::queuePixels(b, (size_t)n * (size_t)wOut, first);
        first = false;
    }
    St77922::wait();
}

bool push(const uint8_t* src, int32_t w, int32_t h, uint8_t rot) {
    if (!src || w <= 0 || h <= 0 || h > St77922::PANEL_H || !ready()) return false;
    const bool full = !s_valid || s_validW != w || s_validH != h || s_validRot != rot || (s_pushes % 64) == 0;
    s_pushes++;
    // A band of logical rows is a band of panel COLUMNS in landscape, and the
    // panel keeps pixels in pairs: windows four rows at a time stay even on
    // both edges (a one-column window smeared upstream's title bar).
    static bool changed[St77922::PANEL_H];
    for (int32_t r = 0; r < h; r++) {
        const uint32_t hv = rowHash(src + (size_t)r * (size_t)w, w);
        changed[r] = full || hv != s_rowHash[r];
        s_rowHash[r] = hv;
    }
    FramePush::RowSpan spans[16];
    const int n = FramePush::frameSpans(changed, h, 4, spans, 16);
    for (int i = 0; i < n; i++) span(src, w, rot, spans[i].r0, spans[i].r1);
    s_valid = true; s_validW = w; s_validH = h; s_validRot = rot;
    return true;
}
}  // namespace St77922Blit
#endif  // FREENOVE_S3_35
