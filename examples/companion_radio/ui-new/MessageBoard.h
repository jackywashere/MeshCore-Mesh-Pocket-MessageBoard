#pragma once

// Full screen "message board" for e-ink companion devices (Heltec MeshPocket).
// Shows the conversation on one channel, newest at the bottom, drawn at the
// panel's native resolution with u8g2 fonts instead of the scaled 128x128 canvas.

#ifdef UI_MESSAGE_BOARD

#include <Arduino.h>
#include <helpers/ui/GxEPDDisplay.h>
#include <U8g2_for_Adafruit_GFX.h>

#ifndef BOARD_HISTORY
  #define BOARD_HISTORY 24             // messages kept in RAM
#endif
#ifndef BOARD_CHANNEL_NAME
  #define BOARD_CHANNEL_NAME "Public"  // channel shown on the board
#endif

#ifndef BOARD_NAME_CHARS
  #define BOARD_NAME_CHARS 5           // other people's names are shortened to this many characters
#endif

#define BOARD_FROM_LEN   32
#define BOARD_TEXT_LEN   168
#define BOARD_MAX_LINES  24
#define BOARD_HEADER_H   14
#define BOARD_TIME_VALID 1735689600UL  // 2025-01-01: clock considered synced after this

class MessageBoard {
  struct Msg {
    uint32_t ts;
    char from[BOARD_FROM_LEN];
    char text[BOARD_TEXT_LEN];
    uint8_t mine;
  };
  struct Line { uint8_t start, len; };

  Msg _msgs[BOARD_HISTORY];
  int _count = 0;
  int _head = -1;      // index of newest message
  int _scroll = 0;     // how many messages the view is scrolled back from the newest
  bool _large = false; // text size (small by default)
  U8G2_FOR_ADAFRUIT_GFX _u8;
  bool _u8_ready = false;
  char _tmp[BOARD_TEXT_LEN + 1];

  // Convert UTF-8 to what the Latin-1 u8g2 fonts can draw. Unsupported
  // characters (emoji etc) become '*', joiners and variation selectors are dropped.
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
        // zero width joiner, variation selectors, skin tones: drop
      } else if (!last_repl) {
        dst[j++] = '*';
        last_repl = true;
      }
      i += n;
    }
    dst[j] = 0;
  }

  int width(const char* s, int len) {
    if (len > BOARD_TEXT_LEN) len = BOARD_TEXT_LEN;
    memcpy(_tmp, s, len);
    _tmp[len] = 0;
    return _u8.getUTF8Width(_tmp);
  }

  static int nextChar(const char* s, int i, int len) {
    i++;
    while (i < len && (s[i] & 0xC0) == 0x80) i++;
    return i;
  }

  // Greedy word wrap with the current font. The first line can be narrower
  // (it shares space with the sender name and time).
  int wrap(const char* s, int first_w, int w, Line* out, int max_lines) {
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
        if (width(s + pos, j - pos) <= avail) {
          best = j;
          i = j;
          if (j >= len) break;
        } else {
          break;
        }
      }
      if (best < 0) {  // single word wider than the line: break between characters
        int j = pos, ok = -1;
        while (j < len) {
          int k = nextChar(s, j, len);
          if (width(s + pos, k - pos) <= avail) { ok = k; j = k; } else break;
        }
        if (ok < 0) {
          if (n == 0 && avail < w) {  // no room next to the name, start on the next line
            out[n].start = pos; out[n].len = 0; n++;
            continue;
          }
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

  void text(GxEPDDisplay& d, int x, int y, const char* s) {
    d.crcAdd(&x, sizeof(x));
    d.crcAdd(&y, sizeof(y));
    d.crcAdd(s, strlen(s));
    _u8.drawUTF8(x, y, s);
  }

  static void fmtTime(char* buf, size_t sz, uint32_t ts, uint32_t now, int tz_hours) {
    if (now >= BOARD_TIME_VALID && ts >= BOARD_TIME_VALID) {
      uint32_t age = now > ts ? now - ts : 0;
      if (age >= 20UL * 3600UL) {
        snprintf(buf, sz, "%lud", (unsigned long)((age + 43200UL) / 86400UL));
      } else {
        int32_t t = (int32_t)ts + tz_hours * 3600;
        snprintf(buf, sz, "%02d:%02d", (int)((t / 3600) % 24), (int)((t / 60) % 60));
      }
    } else {
      uint32_t age = now > ts ? now - ts : 0;
      if (age < 60) snprintf(buf, sz, "now");
      else if (age < 3600) snprintf(buf, sz, "%lum", (unsigned long)(age / 60));
      else snprintf(buf, sz, "%luh", (unsigned long)(age / 3600));
    }
  }

  void drawHeader(GxEPDDisplay& d, int W, const char* chan, uint32_t now, int tz_hours,
                  int batt_pct, bool connected) {
    Adafruit_GFX& g = d.gfx();
    g.fillRect(0, 0, W, BOARD_HEADER_H, GxEPD_BLACK);
    _u8.setForegroundColor(GxEPD_WHITE);
    _u8.setBackgroundColor(GxEPD_BLACK);
    _u8.setFont(u8g2_font_helvB08_tf);
    const int base = 10;

    char buf[40];
    snprintf(buf, sizeof(buf), "# %s", chan);
    text(d, 3, base, buf);

    // battery: icon + percentage, right aligned
    snprintf(buf, sizeof(buf), "%d%%", batt_pct);
    int pw = _u8.getUTF8Width(buf);
    int px = W - 3 - pw;
    text(d, px, base, buf);
    int bx = px - 19, by = 3;
    g.drawRect(bx, by, 14, 8, GxEPD_WHITE);
    g.fillRect(bx + 14, by + 2, 2, 4, GxEPD_WHITE);
    int fill = (batt_pct * 10) / 100;
    g.fillRect(bx + 2, by + 2, fill, 4, GxEPD_WHITE);
    d.crcAdd(&fill, sizeof(fill));
    int right_edge = bx - 6;

    if (connected) {
      const char* bt = "BT";
      int w = _u8.getUTF8Width(bt);
      text(d, right_edge - w, base, bt);
      right_edge -= w + 6;
    }

    // centre: clock, or how far back you have scrolled
    if (_scroll > 0) {
      snprintf(buf, sizeof(buf), "%d newer v", _scroll);
    } else if (now >= BOARD_TIME_VALID) {
      int32_t t = (int32_t)now + tz_hours * 3600;
      snprintf(buf, sizeof(buf), "%02d:%02d", (int)((t / 3600) % 24), (int)((t / 60) % 60));
    } else {
      buf[0] = 0;
    }
    if (buf[0]) {
      int w = _u8.getUTF8Width(buf);
      int x = (W - w) / 2;
      if (x + w > right_edge) x = right_edge - w;
      text(d, x, base, buf);
    }
  }

public:
  void add(const char* from, const char* txt, size_t txt_len, uint32_t ts, bool mine) {
    _head = (_head + 1) % BOARD_HISTORY;
    if (_count < BOARD_HISTORY) _count++;
    Msg& m = _msgs[_head];
    m.ts = ts;
    m.mine = mine ? 1 : 0;
    sanitize(m.from, sizeof(m.from), from, strlen(from));
    sanitize(m.text, sizeof(m.text), txt, txt_len);
    if (_scroll > 0 && _scroll < _count - 1) _scroll++;  // keep a scrolled back view where it is
  }

  // channel messages arrive as "Sender: message"
  void addReceived(const char* raw, uint32_t ts) {
    const char* sep = strstr(raw, ": ");
    if (sep && sep > raw && (sep - raw) < BOARD_FROM_LEN) {
      char from[BOARD_FROM_LEN];
      memcpy(from, raw, sep - raw);
      from[sep - raw] = 0;
      add(from, sep + 2, strlen(sep + 2), ts, false);
    } else {
      add("", raw, strlen(raw), ts, false);
    }
  }

  bool scrollOlder() { if (_scroll < _count - 1) { _scroll++; return true; } return false; }
  bool scrollNewer() { if (_scroll > 0) { _scroll--; return true; } return false; }
  bool isScrolled() const { return _scroll > 0; }
  void resetScroll() { _scroll = 0; }
  void toggleSize() { _large = !_large; }
  bool isLarge() const { return _large; }

  void render(GxEPDDisplay& d, const char* chan, uint32_t now, int tz_hours, int batt_pct, bool connected) {
    Adafruit_GFX& g = d.gfx();
    if (!_u8_ready) { _u8.begin(g); _u8_ready = true; }
    _u8.setFontMode(1);
    _u8.setFontDirection(0);
    d.crcAdd(&_large, sizeof(_large));

    const int W = d.nativeWidth();
    const int H = d.nativeHeight();
    drawHeader(d, W, chan, now, tz_hours, batt_pct, connected);

    _u8.setForegroundColor(GxEPD_BLACK);
    _u8.setBackgroundColor(GxEPD_WHITE);

    const int top = BOARD_HEADER_H + 2;
    if (_count == 0) {
      _u8.setFont(u8g2_font_helvB10_tf);
      char buf[48];
      snprintf(buf, sizeof(buf), "Listening on %s", chan);
      text(d, (W - _u8.getUTF8Width(buf)) / 2, top + (H - top) / 2 - 2, buf);
      _u8.setFont(u8g2_font_helvR08_tf);
      const char* sub = "messages will appear here";
      text(d, (W - _u8.getUTF8Width(sub)) / 2, top + (H - top) / 2 + 14, sub);
      return;
    }

    const uint8_t* f_txt  = _large ? u8g2_font_helvR10_tf : u8g2_font_helvR08_tf;
    const uint8_t* f_name = _large ? u8g2_font_helvB10_tf : u8g2_font_helvB08_tf;
    const uint8_t* f_time = u8g2_font_helvR08_tf;
    _u8.setFont(f_txt);
    const int asc = _u8.getFontAscent();
    const int lh = asc - _u8.getFontDescent() + (_large ? 2 : 1);
    const int L = 2;
    const int avail = W - 2 * L;
    const int gap = 5;

    int y = H - 1;  // lowest free pixel row
    Line lines[BOARD_MAX_LINES];
    for (int k = _scroll; k < _count && y > top; k++) {
      Msg& m = _msgs[(_head - k + BOARD_HISTORY) % BOARD_HISTORY];

      char name[BOARD_FROM_LEN + 1];
      snprintf(name, sizeof(name), "%s", m.from);
      if (!m.mine) {  // shorten other people's names, counting characters not bytes
        int chars = 0, b = 0;
        while (name[b] && chars < BOARD_NAME_CHARS) { b = nextChar(name, b, strlen(name)); chars++; }
        name[b] = 0;
        while (b > 0 && name[b - 1] == ' ') name[--b] = 0;
      }
      _u8.setFont(f_name);
      int name_w = name[0] ? _u8.getUTF8Width(name) + (m.mine ? 10 : 5) : 0;

      char tbuf[12];
      fmtTime(tbuf, sizeof(tbuf), m.ts, now, tz_hours);
      _u8.setFont(f_time);
      int time_w = _u8.getUTF8Width(tbuf);

      _u8.setFont(f_txt);
      int n = wrap(m.text, avail - name_w - time_w - 6, avail, lines, BOARD_MAX_LINES);
      if (n == 0) { lines[0].start = 0; lines[0].len = 0; n = 1; }

      int block_top = y - n * lh + 1;
      for (int i = 0; i < n; i++) {
        int line_top = block_top + i * lh;
        if (line_top < top) continue;  // clipped by the header: show the tail of long messages
        int baseline = line_top + asc;
        int x = L;
        if (i == 0) {
          if (name[0]) {
            _u8.setFont(f_name);
            if (m.mine) {  // your own messages: name in a black tag
              int nw = _u8.getUTF8Width(name);
              g.fillRoundRect(x, line_top - 1, nw + 6, lh + 1, 2, GxEPD_BLACK);
              _u8.setForegroundColor(GxEPD_WHITE);
              _u8.setBackgroundColor(GxEPD_BLACK);
              text(d, x + 3, baseline, name);
              _u8.setForegroundColor(GxEPD_BLACK);
              _u8.setBackgroundColor(GxEPD_WHITE);
            } else {
              text(d, x, baseline, name);
            }
            x += name_w;
          }
          _u8.setFont(f_time);
          text(d, W - L - time_w, baseline, tbuf);
          _u8.setFont(f_txt);
        }
        if (lines[i].len > 0) {
          memcpy(_tmp, m.text + lines[i].start, lines[i].len);
          _tmp[lines[i].len] = 0;
          text(d, x, baseline, _tmp);
        }
      }

      y = block_top - gap;
      int sep_y = block_top - (gap + 1) / 2 - 1;
      if (sep_y > top && k + 1 < _count) {
        for (int x = L; x < W - L; x += 3) g.drawPixel(x, sep_y, GxEPD_BLACK);
      }
    }
  }
};

#endif
