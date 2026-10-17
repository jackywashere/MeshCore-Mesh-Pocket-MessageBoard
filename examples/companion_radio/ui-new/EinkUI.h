#pragma once

// Shared native resolution drawing toolkit for e-ink companion screens (Heltec MeshPocket).
// Every screen uses the same header, footer, fonts and colours so the UI feels like one piece.

#ifdef UI_MESSAGE_BOARD

#include <Arduino.h>
#include <helpers/ui/GxEPDDisplay.h>
#include <U8g2_for_Adafruit_GFX.h>

#define UI_HEADER_H      14
#define UI_FOOTER_H      14
#define UI_TIME_VALID    1735689600UL  // 2025-01-01: clock considered synced after this

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif

struct UiLine { uint8_t start, len; };

class EinkUI {
  bool _ready = false;
  char _tmp[200];

public:
  U8G2_FOR_ADAFRUIT_GFX u8;
  GxEPDDisplay* d = nullptr;
  Adafruit_GFX* g = nullptr;
  uint16_t ink = GxEPD_BLACK, paper = GxEPD_WHITE;
  int W = 250, H = 122;

  static EinkUI& get() { static EinkUI ui; return ui; }

  // Call at the start of every render. rotation is a GFX rotation (0..3).
  void begin(GxEPDDisplay& disp, int rotation, bool dark) {
    d = &disp;
    g = &disp.gfx();
    disp.setNativeRotation(rotation);
    if (!_ready) { u8.begin(*g); _ready = true; }
    W = g->width();
    H = g->height();
    ink = dark ? GxEPD_WHITE : GxEPD_BLACK;
    paper = dark ? GxEPD_BLACK : GxEPD_WHITE;
    g->fillScreen(paper);
    u8.setFontMode(1);
    u8.setFontDirection(0);
    normal();
    uint8_t k[2] = { (uint8_t)rotation, (uint8_t)dark };
    disp.crcAdd(k, 2);
  }

  static int battPercent(uint16_t mv) {
    int pct = ((int)mv - BATT_MIN_MILLIVOLTS) * 100 / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
  }

  void crc(const void* p, size_t n) { if (d) d->crcAdd(p, n); }
  void crc4(int a, int b = 0, int c = 0, int e = 0) { int v[4] = { a, b, c, e }; crc(v, sizeof(v)); }

  // colours: normal = ink on paper, inverse = paper on ink
  void normal()  { u8.setForegroundColor(ink);   u8.setBackgroundColor(paper); }
  void inverse() { u8.setForegroundColor(paper); u8.setBackgroundColor(ink); }

  void font(const uint8_t* f) { u8.setFont(f); }
  int width(const char* s) { return u8.getUTF8Width(s); }
  int ascent() { return u8.getFontAscent(); }
  int descent() { return u8.getFontDescent(); }

  void text(int x, int y, const char* s) {
    crc4(x, y);
    crc(s, strlen(s));
    u8.drawUTF8(x, y, s);
  }
  void textR(int xr, int y, const char* s) { text(xr - width(s), y, s); }
  void textC(int xc, int y, const char* s) { text(xc - width(s) / 2, y, s); }

  void fill(int x, int y, int w, int h, bool inv = false) { crc4(x, y, w, h); g->fillRect(x, y, w, h, inv ? paper : ink); }
  void frame(int x, int y, int w, int h) { crc4(x, y, w, h); g->drawRect(x, y, w, h, ink); }
  void fillRound(int x, int y, int w, int h, int r, bool inv = false) { crc4(x, y, w, h); g->fillRoundRect(x, y, w, h, r, inv ? paper : ink); }
  void disc(int x, int y, int r, bool inv = false) { crc4(x, y, r); g->fillCircle(x, y, r, inv ? paper : ink); }
  void ring(int x, int y, int r, int thick = 1) { crc4(x, y, r, thick); for (int i = 0; i < thick; i++) g->drawCircle(x, y, r - i, ink); }
  void line(int x0, int y0, int x1, int y1) { crc4(x0, y0, x1, y1); g->drawLine(x0, y0, x1, y1, ink); }
  void pixel(int x, int y, bool inv = false) { g->drawPixel(x, y, inv ? paper : ink); }
  void hdots(int x0, int x1, int y, int step = 3) { crc4(x0, x1, y, step); for (int x = x0; x < x1; x += step) g->drawPixel(x, y, ink); }
  void vdots(int x, int y0, int y1, int step = 3) { crc4(x, y0, y1, step); for (int y = y0; y < y1; y += step) g->drawPixel(x, y, ink); }

  // thick arc, angles in degrees, 0 = right, counter-clockwise, screen y down
  void arc(int cx, int cy, int r, int a0, int a1, int thick = 2) {
    crc4(cx, cy, r, a0 * 1000 + a1);
    for (int t = 0; t < thick; t++) {
      int rr = r - t;
      for (int a = a0 * 4; a <= a1 * 4; a++) {
        float rad = a * 0.25f * 0.0174533f;
        g->drawPixel(cx + (int)lroundf(cosf(rad) * rr), cy - (int)lroundf(sinf(rad) * rr), ink);
      }
    }
  }

  // UTF-8 to what the Latin-1 u8g2 fonts can draw. Emoji become '*'.
  static void sanitize(char* dst, size_t dsz, const char* src, size_t srclen) {
    size_t i = 0, j = 0;
    bool last_repl = false;
    while (i < srclen && src[i] && j + 3 < dsz) {
      uint8_t c = (uint8_t)src[i];
      if (c < 0x80) {
        dst[j++] = (c < 0x20) ? ' ' : (char)c;
        i++;
        last_repl = false;
        continue;
      }
      int n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
      if (i + n > srclen) break;
      uint32_t cp = 0;
      if (n == 2) cp = ((c & 0x1F) << 6) | (src[i+1] & 0x3F);
      else if (n == 3) cp = ((c & 0x0F) << 12) | ((src[i+1] & 0x3F) << 6) | (src[i+2] & 0x3F);
      else if (n == 4) cp = ((c & 0x07) << 18) | ((src[i+1] & 0x3F) << 12) | ((src[i+2] & 0x3F) << 6) | (src[i+3] & 0x3F);
      if (n == 2 && cp >= 0xA0 && cp <= 0xFF) {
        dst[j++] = src[i];
        dst[j++] = src[i+1];
        last_repl = false;
      } else if (cp == 0x200D || cp == 0xFE0F || cp == 0xFE0E || (cp >= 0x1F3FB && cp <= 0x1F3FF)) {
        // joiners, variation selectors, skin tones: drop
      } else if (!last_repl) {
        dst[j++] = '*';
        last_repl = true;
      }
      i += n;
    }
    dst[j] = 0;
  }

  static int nextChar(const char* s, int i, int len) {
    i++;
    while (i < len && (s[i] & 0xC0) == 0x80) i++;
    return i;
  }

  // keep the first n characters (n <= 0 keeps everything), trims trailing spaces
  static void truncChars(char* s, int n) {
    if (n <= 0) return;
    int len = strlen(s), chars = 0, b = 0;
    while (b < len && chars < n) { b = nextChar(s, b, len); chars++; }
    s[b] = 0;
    while (b > 0 && s[b - 1] == ' ') s[--b] = 0;
  }

  int widthN(const char* s, int len) {
    if (len >= (int)sizeof(_tmp)) len = sizeof(_tmp) - 1;
    memcpy(_tmp, s, len);
    _tmp[len] = 0;
    return u8.getUTF8Width(_tmp);
  }

  void textN(int x, int y, const char* s, int len) {
    if (len >= (int)sizeof(_tmp)) len = sizeof(_tmp) - 1;
    memcpy(_tmp, s, len);
    _tmp[len] = 0;
    text(x, y, _tmp);
  }

  // Greedy word wrap with the current font. The first line can be narrower.
  int wrap(const char* s, int first_w, int w, UiLine* out, int max_lines) {
    int len = strlen(s);
    int pos = 0, n = 0;
    while (pos < len && n < max_lines) {
      int avail = (n == 0) ? first_w : w;
      if (n > 0) while (pos < len && s[pos] == ' ') pos++;
      if (pos >= len) break;
      int best = -1, i = pos;
      while (true) {
        int j = i;
        while (j < len && s[j] == ' ') j++;
        while (j < len && s[j] != ' ') j++;
        if (j == i) break;
        if (widthN(s + pos, j - pos) <= avail) {
          best = j;
          i = j;
          if (j >= len) break;
        } else {
          break;
        }
      }
      if (best < 0) {
        int j = pos, ok = -1;
        while (j < len) {
          int k = nextChar(s, j, len);
          if (widthN(s + pos, k - pos) <= avail) { ok = k; j = k; } else break;
        }
        if (ok < 0) {
          if (n == 0 && avail < w) { out[n].start = pos; out[n].len = 0; n++; continue; }
          ok = nextChar(s, pos, len);
        }
        best = ok;
      }
      out[n].start = pos;
      out[n].len = best - pos;
      n++;
      pos = best;
    }
    return n;
  }

  static void fmtClock(char* buf, size_t sz, uint32_t t, int tz_hours) {
    int32_t l = (int32_t)t + tz_hours * 3600;
    snprintf(buf, sz, "%02d:%02d", (int)((l / 3600) % 24), (int)((l / 60) % 60));
  }

  static void fmtAge(char* buf, size_t sz, uint32_t secs) {
    if (secs < 60) snprintf(buf, sz, "now");
    else if (secs < 3600) snprintf(buf, sz, "%lum", (unsigned long)(secs / 60));
    else if (secs < 86400) snprintf(buf, sz, "%luh", (unsigned long)(secs / 3600));
    else snprintf(buf, sz, "%lud", (unsigned long)(secs / 86400));
  }

  // "Sat 17 Oct"
  static void fmtDate(char* buf, size_t sz, uint32_t t, int tz_hours) {
    static const char* wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char* mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int32_t l = (int32_t)t + tz_hours * 3600;
    int32_t days = l / 86400;
    int w = (days + 4) % 7;
    // civil from days (Howard Hinnant)
    int32_t z = days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = z - era * 146097;
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mm = mp < 10 ? mp + 3 : mp - 9;
    snprintf(buf, sz, "%s %lu %s", wd[w], (unsigned long)dd, mo[mm - 1]);
  }

  // Black title bar: title left, clock centre (when it fits), Bluetooth + battery right
  void header(const char* title, uint32_t now, int tz_hours, int batt_pct, bool connected, const char* centre = nullptr) {
    fill(0, 0, W, UI_HEADER_H);
    inverse();
    font(u8g2_font_helvB08_tf);
    const int base = 10;
    text(3, base, title);
    int title_end = 3 + width(title);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d%%", batt_pct);
    int px = W - 3 - width(buf);
    text(px, base, buf);
    int bx = px - 18;
    g->drawRect(bx, 3, 13, 8, paper);
    g->fillRect(bx + 13, 5, 2, 4, paper);
    int f = (batt_pct * 9) / 100;
    g->fillRect(bx + 2, 5, f, 4, paper);
    crc4(f);
    int right = bx - 5;

    if (connected && W > 200) {
      int w = width("BT");
      text(right - w, base, "BT");
      right -= w + 6;
    }

    const char* c = centre;
    if (!c && now >= UI_TIME_VALID) { fmtClock(buf, sizeof(buf), now, tz_hours); c = buf; }
    if (c && c[0]) {
      int w = width(c);
      int x = (W - w) / 2;
      if (x + w > right) x = right - w;
      if (x > title_end + 6) text(x, base, c);
    }
    normal();
  }

  // dotted rule, hint text on the left, page dots on the right (page < 0: no dots)
  void footer(const char* hint, int page, int count) {
    fill(0, H - UI_FOOTER_H, W, UI_FOOTER_H, true);
    hdots(2, W - 2, H - UI_FOOTER_H);
    font(u8g2_font_helvR08_tf);
    normal();
    text(3, H - 3, hint);
    if (page >= 0) {
      int step = W > 200 ? 7 : 5;
      int x = W - 4 - (count - 1) * step;
      for (int i = 0; i < count; i++, x += step) {
        if (i == page) {
          if (step == 7) g->fillRect(x - 2, H - 9, 5, 5, ink); else g->fillRect(x - 1, H - 8, 3, 3, ink);
        } else {
          if (step == 7) g->drawRect(x - 1, H - 8, 3, 3, ink); else g->drawPixel(x, H - 7, ink);
        }
      }
      crc4(page, count);
    }
  }

  int bodyTop() const { return UI_HEADER_H + 2; }
  int bodyBottom() const { return H - UI_FOOTER_H - 1; }
};

#endif
