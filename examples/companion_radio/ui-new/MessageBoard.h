#pragma once

// Full screen "message board" for e-ink companion devices (Heltec MeshPocket).
// Shows the conversation on one channel, newest at the bottom, drawn at the
// panel's native resolution.

#ifdef UI_MESSAGE_BOARD

#include "EinkUI.h"

#ifndef BOARD_HISTORY
  #define BOARD_HISTORY 24             // messages kept in RAM
#endif

#define BOARD_FROM_LEN   32
#define BOARD_TEXT_LEN   168
#define BOARD_MAX_LINES  24

class MessageBoard {
  struct Msg {
    uint32_t ts;
    char from[BOARD_FROM_LEN];
    char text[BOARD_TEXT_LEN];
    uint8_t mine;
  };

  Msg _msgs[BOARD_HISTORY];
  int _count = 0;
  int _head = -1;      // index of newest message
  int _scroll = 0;     // how many messages the view is scrolled back from the newest

  static void fmtTime(char* buf, size_t sz, uint32_t ts, uint32_t now, int tz_hours) {
    uint32_t age = now > ts ? now - ts : 0;
    if (now >= UI_TIME_VALID && ts >= UI_TIME_VALID) {
      if (age >= 20UL * 3600UL) snprintf(buf, sz, "%lud", (unsigned long)((age + 43200UL) / 86400UL));
      else EinkUI::fmtClock(buf, sz, ts, tz_hours);
    } else {
      EinkUI::fmtAge(buf, sz, age);
    }
  }

public:
  void clear() { _count = 0; _head = -1; _scroll = 0; }

  void add(const char* from, const char* txt, size_t txt_len, uint32_t ts, bool mine) {
    _head = (_head + 1) % BOARD_HISTORY;
    if (_count < BOARD_HISTORY) _count++;
    Msg& m = _msgs[_head];
    m.ts = ts;
    m.mine = mine ? 1 : 0;
    EinkUI::sanitize(m.from, sizeof(m.from), from, strlen(from));
    EinkUI::sanitize(m.text, sizeof(m.text), txt, txt_len);
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

  void render(EinkUI& ui, const char* chan, uint32_t now, int tz_hours, int batt_pct, bool connected,
              bool large, int name_chars) {
    char title[40];
    snprintf(title, sizeof(title), "# %s", chan);
    char centre[20];
    centre[0] = 0;
    if (_scroll > 0) snprintf(centre, sizeof(centre), "%d newer v", _scroll);
    ui.header(title, now, tz_hours, batt_pct, connected, _scroll > 0 ? centre : nullptr);

    const int W = ui.W, H = ui.H;
    const int top = UI_HEADER_H + 2;
    if (_count == 0) {
      ui.font(u8g2_font_helvB10_tf);
      char buf[48];
      snprintf(buf, sizeof(buf), "Listening on %s", chan);
      ui.textC(W / 2, top + (H - top) / 2 - 2, buf);
      ui.font(u8g2_font_helvR08_tf);
      ui.textC(W / 2, top + (H - top) / 2 + 14, "messages will appear here");
      return;
    }

    const uint8_t* f_txt  = large ? u8g2_font_helvR10_tf : u8g2_font_helvR08_tf;
    const uint8_t* f_name = large ? u8g2_font_helvB10_tf : u8g2_font_helvB08_tf;
    const uint8_t* f_time = u8g2_font_helvR08_tf;
    ui.font(f_txt);
    const int asc = ui.ascent();
    const int lh = asc - ui.descent() + (large ? 2 : 1);
    const int L = 2;
    const int avail = W - 2 * L;
    const int gap = 5;

    int y = H - 1;  // lowest free pixel row
    UiLine lines[BOARD_MAX_LINES];
    for (int k = _scroll; k < _count && y > top; k++) {
      Msg& m = _msgs[(_head - k + BOARD_HISTORY) % BOARD_HISTORY];

      char name[BOARD_FROM_LEN + 1];
      snprintf(name, sizeof(name), "%s", m.from);
      if (!m.mine) EinkUI::truncChars(name, name_chars);
      ui.font(f_name);
      int name_w = name[0] ? ui.width(name) + (m.mine ? 10 : 5) : 0;

      char tbuf[12];
      fmtTime(tbuf, sizeof(tbuf), m.ts, now, tz_hours);
      ui.font(f_time);
      int time_w = ui.width(tbuf);

      ui.font(f_txt);
      int n = ui.wrap(m.text, avail - name_w - time_w - 6, avail, lines, BOARD_MAX_LINES);
      if (n == 0) { lines[0].start = 0; lines[0].len = 0; n = 1; }

      int block_top = y - n * lh + 1;
      for (int i = 0; i < n; i++) {
        int line_top = block_top + i * lh;
        if (line_top < top) continue;  // clipped by the header: show the tail of long messages
        int baseline = line_top + asc;
        int x = L;
        if (i == 0) {
          if (name[0]) {
            ui.font(f_name);
            if (m.mine) {  // your own messages: name in a tag
              int nw = ui.width(name);
              ui.fillRound(x, line_top - 1, nw + 6, lh + 1, 2);
              ui.inverse();
              ui.text(x + 3, baseline, name);
              ui.normal();
            } else {
              ui.text(x, baseline, name);
            }
            x += name_w;
          }
          ui.font(f_time);
          ui.text(W - L - time_w, baseline, tbuf);
          ui.font(f_txt);
        }
        if (lines[i].len > 0) ui.textN(x, baseline, m.text + lines[i].start, lines[i].len);
      }

      y = block_top - gap;
      int sep_y = block_top - (gap + 1) / 2 - 1;
      if (sep_y > top && k + 1 < _count) ui.hdots(L, W - L, sep_y);
    }
  }
};

#endif
