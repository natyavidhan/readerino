#include "Tft.h"
#include "Config.h"
#include "Theme.h"
#include "Font.h"
#include <avr/pgmspace.h>

namespace {
  // ST7735R init sequence from Adafruit_ST7735 (Rcmd1 + Rcmd3, BSD
  // license): count, then per command: opcode, arg count (| DELAY_FLAG if a
  // delay byte follows the args), args, [delay ms, 255 = 500ms].
  const uint8_t DELAY_FLAG = 0x80;
  const uint8_t INIT_CMDS[] PROGMEM = {
    18,
    0x01, DELAY_FLAG, 150,                   // SWRESET
    0x11, DELAY_FLAG, 255,                   // SLPOUT
    0xB1, 3, 0x01, 0x2C, 0x2D,               // FRMCTR1
    0xB2, 3, 0x01, 0x2C, 0x2D,               // FRMCTR2
    0xB3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D, // FRMCTR3
    0xB4, 1, 0x07,                           // INVCTR: no inversion
    0xC0, 3, 0xA2, 0x02, 0x84,               // PWCTR1
    0xC1, 1, 0xC5,                           // PWCTR2
    0xC2, 2, 0x0A, 0x00,                     // PWCTR3
    0xC3, 2, 0x8A, 0x2A,                     // PWCTR4
    0xC4, 2, 0x8A, 0xEE,                     // PWCTR5
    0xC5, 1, 0x0E,                           // VMCTR1
    0x20, 0,                                 // INVOFF
    0x36, 1, TFT_MADCTL,                     // MADCTL: orientation (Config.h)
    0x3A, 1, 0x05,                           // COLMOD: 16-bit color
    0xE0, 16, 0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, // GMCTRP1
              0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10,
    0xE1, 16, 0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, // GMCTRN1
              0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10,
    0x29, DELAY_FLAG, 100,                   // DISPON
  };

  volatile uint8_t *csPort, *dcPort;
  uint8_t csMask, dcMask;

  inline void spi(uint8_t b) {
    SPDR = b;
    while (!(SPSR & _BV(SPIF))) {}
  }

  inline void pixel(uint16_t c) {
    spi(c >> 8);
    spi(c);
  }

  void command(uint8_t c) {
    *dcPort &= ~dcMask;
    spi(c);
    *dcPort |= dcMask;
  }

  // Starts a transaction and opens an address window; everything sent
  // until end() is pixel data filling it row by row.
  void window(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    // Re-assert mode 0 at F_CPU/2 (8MHz) every time: the SD library calls
    // SPI.beginTransaction() internally even in its software-SPI mode,
    // which reprograms these registers to its own (slower) settings.
    SPCR = _BV(SPE) | _BV(MSTR);
    SPSR = _BV(SPI2X);
    *csPort &= ~csMask;
    command(0x2A); // CASET
    spi(0); spi(x); spi(0); spi(x + w - 1);
    command(0x2B); // RASET
    spi(0); spi(y); spi(0); spi(y + h - 1);
    command(0x2C); // RAMWR
  }

  void end() { *csPort |= csMask; }

  void textBoxImpl(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
                   const char *text, bool inFlash, uint16_t fg, uint16_t bg) {
    if (!w || !h) return;
    window(x, y, w, h);
    for (uint8_t r = 0; r < h; r++) {
      uint8_t c = 0;
      uint8_t gy = r - ty; // wraps past GLYPH_H when r < ty
      if (gy < GLYPH_H) {
        uint8_t bit = 1 << gy;
        for (; c < tx && c < w; c++) pixel(bg);
        for (const char *p = text; c < w; p++) {
          uint8_t code = inFlash ? pgm_read_byte(p) : *p;
          if (!code) break;
          if (code < FONT_FIRST || code > FONT_LAST) code = '?';
          const uint8_t *glyph = FONT + (code - FONT_FIRST) * 5;
          for (uint8_t col = 0; col < GLYPH_W && c < w; col++, c++) {
            bool on = col < 5 && (pgm_read_byte(glyph + col) & bit);
            pixel(on ? fg : bg);
          }
        }
      }
      for (; c < w; c++) pixel(bg);
    }
    end();
  }
}

void Tft::begin() {
  pinMode(PIN_TFT_LED, OUTPUT);
  digitalWrite(PIN_TFT_LED, HIGH);

  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  csPort = portOutputRegister(digitalPinToPort(PIN_TFT_CS));
  csMask = digitalPinToBitMask(PIN_TFT_CS);
  dcPort = portOutputRegister(digitalPinToPort(PIN_TFT_DC));
  dcMask = digitalPinToBitMask(PIN_TFT_DC);
  *csPort |= csMask;
  *dcPort |= dcMask;

  // Hardware SPI master: MOSI (D11) and SCK (D13) outputs. D10 (the
  // chip's SS pin) must also be an output or a LOW on it would drop the
  // SPI peripheral into slave mode -- it's the SD card's CS, so drive it
  // HIGH (deselected) here too.
  pinMode(11, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(10);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(120);

  SPCR = _BV(SPE) | _BV(MSTR);
  SPSR = _BV(SPI2X);
  const uint8_t *p = INIT_CMDS;
  uint8_t count = pgm_read_byte(p++);
  *csPort &= ~csMask;
  while (count--) {
    command(pgm_read_byte(p++));
    uint8_t args = pgm_read_byte(p++);
    bool hasDelay = args & DELAY_FLAG;
    args &= ~DELAY_FLAG;
    while (args--) spi(pgm_read_byte(p++));
    if (hasDelay) {
      uint8_t ms = pgm_read_byte(p++);
      delay(ms == 255 ? 500 : ms);
    }
  }
  end();
}

void Tft::fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  if (!w || !h) return;
  window(x, y, w, h);
  uint8_t hi = color >> 8, lo = color;
  for (uint16_t n = (uint16_t)w * h; n; n--) {
    spi(hi);
    spi(lo);
  }
  end();
}

void Tft::textBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
                  const char *text, uint16_t fg, uint16_t bg) {
  textBoxImpl(x, y, w, h, tx, ty, text, false, fg, bg);
}

void Tft::textBoxP(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tx, uint8_t ty,
                   const char *text, uint16_t fg, uint16_t bg) {
  textBoxImpl(x, y, w, h, tx, ty, text, true, fg, bg);
}
