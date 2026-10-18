#pragma once

// Native resolution screens for the Heltec MeshPocket companion UI:
// splash, the paged home screen (board, status, heard, map, radio, bluetooth,
// advert, discover, settings, power), direct message popup and alerts.
// Included from UITask.cpp (needs the_mesh, rtc_clock, radio_driver, display).

#ifdef UI_MESSAGE_BOARD

#include "EinkUI.h"
#include "MapView.h"
#include "UiSettings.h"

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

static int uiRotation(const UiSettings& s, bool portrait) {
  int r = portrait ? (DISPLAY_ROTATION + 1) % 4 : DISPLAY_ROTATION;
  return s.flip ? (r + 2) % 4 : r;
}

// shorten text with "..." so it fits max_w in the current font
static void fitText(EinkUI& ui, char* s, int max_w) {
  if (ui.width(s) <= max_w) return;
  int len = strlen(s);
  while (len > 0) {
    len--;
    while (len > 0 && (s[len] & 0xC0) == 0x80) len--;
    s[len] = 0;
    char tmp[200];
    snprintf(tmp, sizeof(tmp), "%s...", s);
    if (ui.width(tmp) <= max_w) { strcpy(s, tmp); return; }
  }
}

static void drawAlertBox(EinkUI& ui, const char* msg) {
  ui.font(u8g2_font_helvB12_tf);
  int w = ui.width(msg) + 36;
  if (w > ui.W - 16) w = ui.W - 16;
  int h = 34;
  int x = (ui.W - w) / 2, y = (ui.H - h) / 2;
  ui.fill(x + 3, y + 3, w, h);
  ui.fill(x, y, w, h, true);
  ui.frame(x, y, w, h);
  ui.frame(x + 1, y + 1, w - 2, h - 2);
  ui.textC(ui.W / 2, y + 23, msg);
}

// ------------------------------------------------------------------------------------------

class NativeSplash : public UIScreen {
  UITask* _task;
  unsigned long _dismiss;
  char _ver[16];

public:
  NativeSplash(UITask* task) : _task(task) {
    const char* v = FIRMWARE_VERSION;
    const char* dash = strchr(v, '-');
    int len = dash ? dash - v : strlen(v);
    if (len >= (int)sizeof(_ver)) len = sizeof(_ver) - 1;
    memcpy(_ver, v, len);
    _ver[len] = 0;
    _dismiss = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    EinkUI& ui = EinkUI::get();
    UiSettings& s = _task->getUiSettings();
    ui.begin((GxEPDDisplay&)display, uiRotation(s, false), s.dark);
    ui.font(u8g2_font_helvB18_tf);
    ui.textC(ui.W / 2, 52, "MeshCore");
    ui.font(u8g2_font_helvR10_tf);
    ui.textC(ui.W / 2, 74, _ver);
    ui.font(u8g2_font_helvR08_tf);
    ui.textC(ui.W / 2, 92, "message board edition");
    return 1000;
  }

  void poll() override {
    if (millis() >= _dismiss) _task->gotoHomeScreen();
  }
};

// ------------------------------------------------------------------------------------------

class NativeMsgPreview : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  struct Entry {
    uint32_t ts;
    uint8_t path_len;
    char from[33];
    char msg[170];
  };
  #define NATIVE_MAX_UNREAD 16
  Entry _e[NATIVE_MAX_UNREAD];
  int _num = 0;
  int _head = NATIVE_MAX_UNREAD - 1;

public:
  NativeMsgPreview(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { }

  void addPreview(uint8_t path_len, const char* from, const char* msg) {
    _head = (_head + 1) % NATIVE_MAX_UNREAD;
    if (_num < NATIVE_MAX_UNREAD) _num++;
    Entry& e = _e[_head];
    e.ts = _rtc->getCurrentTime();
    e.path_len = path_len;
    EinkUI::sanitize(e.from, sizeof(e.from), from, strlen(from));
    EinkUI::sanitize(e.msg, sizeof(e.msg), msg, strlen(msg));
  }

  int render(DisplayDriver& display) override {
    EinkUI& ui = EinkUI::get();
    UiSettings& s = _task->getUiSettings();
    ui.begin((GxEPDDisplay&)display, uiRotation(s, false), s.dark);
    uint32_t now = _task->snapTime();
    char title[24];
    snprintf(title, sizeof(title), _num > 1 ? "Message  %d unread" : "Message", _num);
    ui.header(title, now, _task->tzHours(), EinkUI::battPercent(_task->snapMilliVolts()),
              _task->hasConnection());
    if (_num == 0) { ui.footer("click: back", -1, 0); return 10000; }

    Entry& e = _e[_head];
    int top = ui.bodyTop();
    char meta[24], t[12];
    if (now >= UI_TIME_VALID && e.ts >= UI_TIME_VALID) EinkUI::fmtClock(t, sizeof(t), e.ts, _task->tzHours());
    else EinkUI::fmtAge(t, sizeof(t), now - e.ts);
    if (e.path_len == 0xFF) snprintf(meta, sizeof(meta), "direct  %s", t);
    else snprintf(meta, sizeof(meta), "%d hop%s  %s", e.path_len, e.path_len == 1 ? "" : "s", t);
    ui.font(u8g2_font_helvR08_tf);
    int mw = ui.width(meta);
    ui.text(ui.W - 3 - mw, top + 11, meta);
    ui.font(u8g2_font_helvB10_tf);
    char from[33];
    snprintf(from, sizeof(from), "%s", e.from);
    while (from[0] && ui.width(from) > ui.W - mw - 12) from[strlen(from) - 1] = 0;
    ui.text(3, top + 11, from);
    ui.hdots(3, ui.W - 3, top + 15);

    ui.font(u8g2_font_helvR10_tf);
    UiLine lines[8];
    int n = ui.wrap(e.msg, ui.W - 6, ui.W - 6, lines, 8);
    int y = top + 30;
    int maxy = ui.bodyBottom();
    for (int i = 0; i < n && y <= maxy; i++, y += 14) ui.textN(3, y, e.msg + lines[i].start, lines[i].len);
    ui.footer(_num > 1 ? "click: next   hold: clear all" : "click: done", -1, 0);
    return 10000;
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _head = (_head + NATIVE_MAX_UNREAD - 1) % NATIVE_MAX_UNREAD;
      display.forceFullRefresh();   // clean flash between messages
      if (--_num <= 0) { _num = 0; _task->gotoHomeScreen(); }
      return true;
    }
    if (c == KEY_ENTER || c == KEY_PREV || c == KEY_LEFT) {
      _num = 0;
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ------------------------------------------------------------------------------------------

class NativeHomeScreen : public UIScreen {
public:
  enum Page { BOARD, MAP, STATUS, HEARD, RADIO, BLUETOOTH, ADVERT, DISCOVER, SETTINGS, POWER, COUNT };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  uint8_t _page = BOARD;
  bool _shutdown_init = false;
  unsigned long _last_input = 0;
  bool _map_mode = false;
  bool _edit = false;
  int _edit_item = 0;
  uint32_t _last_advert = 0;
  AdvertPath _recent[UI_RECENT_LIST_SIZE];
  MapView _map;

#if UI_DISCOVER_SCREEN
  DiscoveredNode _disc[DISCOVERED_NODES_TABLE_SIZE];
  uint32_t _disc_tag = 0, _disc_count = 0, _disc_time = 0;
#endif

  static const int SETTINGS_COUNT = 9;
  static int itemAt(int pos) { static const int order[] = { 0, 8, 1, 2, 3, 4, 5, 6, 7 }; return order[pos % SETTINGS_COUNT]; }

  UiSettings& S() { return _task->getUiSettings(); }
  uint32_t now() { return _task->snapTime(); }          // frozen between events
  uint32_t liveNow() { return _rtc->getCurrentTime(); }
  int tz() { return _task->tzHours(); }
  int batt() { return EinkUI::battPercent(_task->snapMilliVolts()); }

  void goPage(int p) {
    int prev = _page;
    _page = (p + COUNT) % COUNT;
    _map_mode = false;
    if (_page != prev) display.forceFullRefresh();   // clean flash on every page change
    if (_page == MAP) _map.collect();
  }

  // ---- settings helpers
  const char* channelName(int idx, ChannelDetails& tmp) {
    if (the_mesh.getChannel(idx, tmp) && tmp.name[0]) return tmp.name;
    return "Public";
  }
  int nextChannel(int idx) {
    ChannelDetails tmp;
    for (int i = 1; i <= MAX_GROUP_CHANNELS; i++) {
      int j = (idx + i) % MAX_GROUP_CHANNELS;
      if (the_mesh.getChannel(j, tmp) && tmp.name[0]) return j;
    }
    return 0;
  }
  void settingLabel(int i, char* lab, char* val, size_t vsz) {
    UiSettings& s = S();
    ChannelDetails tmp;
    switch (i) {
      case 0: strcpy(lab, "Board channel"); snprintf(val, vsz, "%s", channelName(s.board_channel, tmp)); break;
      case 1: strcpy(lab, "Text size"); snprintf(val, vsz, "%s", s.text_large ? "Large" : "Small"); break;
      case 2: strcpy(lab, "Name length");
        if (UiSettings::nameLen(s.name_len) == 0) snprintf(val, vsz, "Full");
        else snprintf(val, vsz, "%d letters", UiSettings::nameLen(s.name_len));
        break;
      case 3: strcpy(lab, "Flip screen"); snprintf(val, vsz, "%s", s.flip ? "On" : "Off"); break;
      case 4: strcpy(lab, "Dark mode"); snprintf(val, vsz, "%s", s.dark ? "On" : "Off"); break;
      case 5: strcpy(lab, "Back to board"); snprintf(val, vsz, "%s", UiSettings::idleLabel(s.idle)); break;
      case 6: strcpy(lab, "Clean refresh");
        if (UiSettings::refreshEvery(s.refresh) < 0) snprintf(val, vsz, "Never");
        else snprintf(val, vsz, "Every %d", UiSettings::refreshEvery(s.refresh));
        break;
      case 7: strcpy(lab, "Map layout"); snprintf(val, vsz, "%s", s.map_portrait ? "Portrait" : "Landscape"); break;
      case 8: {
        strcpy(lab, "Time zone");
        int h = _task->tzHours();
        snprintf(val, vsz, "%s %s%d", UiSettings::tzLabel(s.tz_mode), h < 0 ? "-" : "+", h < 0 ? -h : h);
        break;
      }
    }
  }
  void changeSetting(int i) {
    UiSettings& s = S();
    switch (i) {
      case 0: s.board_channel = nextChannel(s.board_channel); if (_task->getMessageBoard()) _task->getMessageBoard()->clear(); break;
      case 1: s.text_large ^= 1; break;
      case 2: s.name_len = (s.name_len + 1) % UiSettings::NAME_LEN_COUNT; break;
      case 3: s.flip ^= 1; display.forceFullRefresh(); break;
      case 4: s.dark ^= 1; display.forceFullRefresh(); break;
      case 5: s.idle = (s.idle + 1) % UiSettings::IDLE_COUNT; break;
      case 6: s.refresh = (s.refresh + 1) % UiSettings::REFRESH_COUNT;
        display.setMaxPartialRefresh(UiSettings::refreshEvery(s.refresh));
        break;
      case 7: s.map_portrait ^= 1; break;
      case 8: s.tz_mode = (s.tz_mode + 1) % UiSettings::TZ_COUNT; _task->snapshot(); break;
    }
  }

  // ---- page renderers
  void pageStatus(EinkUI& ui) {
    int top = ui.bodyTop(), bot = ui.bodyBottom();
    int mid = (top + bot) / 2;
    char buf[32];
    if (liveNow() >= UI_TIME_VALID) {
      EinkUI::fmtClock(buf, sizeof(buf), liveNow(), tz());
      ui.font(u8g2_font_helvB24_tf);
      ui.textC(63, mid + 8, buf);
      EinkUI::fmtDate(buf, sizeof(buf), liveNow(), tz());
      ui.font(u8g2_font_helvR10_tf);
      ui.textC(63, mid + 25, buf);
    } else {
      ui.font(u8g2_font_helvB24_tf);
      ui.textC(63, mid + 8, "--:--");
      ui.font(u8g2_font_helvR08_tf);
      ui.textC(63, mid + 24, "connect the app to set");
    }
    ui.vdots(127, top + 3, bot - 1);
    int y = top + 2;
    int n = _task->getMsgCount();
    snprintf(buf, sizeof(buf), n == 1 ? "1 message" : "%d messages", n);
    const char* rows[3][2] = { { "Unread", buf }, { "Phone", _task->hasConnection() ? "Connected" : "Not connected" },
                               { "Node", _prefs->node_name } };
    for (int i = 0; i < 3; i++, y += 30) {
      ui.font(u8g2_font_helvR08_tf);
      ui.text(136, y + 9, rows[i][0]);
      ui.font(u8g2_font_helvB10_tf);
      char v[33];
      EinkUI::sanitize(v, sizeof(v), rows[i][1], strlen(rows[i][1]));
      while (v[0] && ui.width(v) > ui.W - 140) v[strlen(v) - 1] = 0;
      ui.text(136, y + 22, v);
    }
  }

  void pageHeard(EinkUI& ui) {
    the_mesh.getRecentlyHeard(_recent, UI_RECENT_LIST_SIZE);
    int y = ui.bodyTop();
    int shown = 0;
    for (int i = 0; i < UI_RECENT_LIST_SIZE && y + 15 <= ui.bodyBottom() + 2; i++) {
      AdvertPath& a = _recent[i];
      if (a.name[0] == 0) continue;
      char age[12], right[24], name[33];
      EinkUI::fmtAge(age, sizeof(age), now() - a.recv_timestamp);
      int hops = a.path_len & 63;
      if (hops == 0) snprintf(right, sizeof(right), "direct  %s", age);
      else snprintf(right, sizeof(right), "%d hop%s  %s", hops, hops == 1 ? "" : "s", age);
      ui.font(u8g2_font_helvR08_tf);
      int rw = ui.width(right);
      ui.text(ui.W - 3 - rw, y + 11, right);
      ui.font(u8g2_font_helvR10_tf);
      EinkUI::sanitize(name, sizeof(name), a.name, strlen(a.name));
      while (name[0] && ui.width(name) > ui.W - rw - 12) name[strlen(name) - 1] = 0;
      ui.text(3, y + 11, name);
      y += 15;
      shown++;
    }
    if (!shown) {
      ui.font(u8g2_font_helvR10_tf);
      ui.textC(ui.W / 2, (ui.bodyTop() + ui.bodyBottom()) / 2 + 4, "Nothing heard yet");
    }
  }

  void pageRadio(EinkUI& ui) {
    char v[6][16];
    snprintf(v[0], 16, "%.3f", _prefs->freq);
    snprintf(v[1], 16, _prefs->bw == (int)_prefs->bw ? "%.0f" : "%.1f", _prefs->bw);
    snprintf(v[2], 16, "SF %d", _prefs->sf);
    snprintf(v[3], 16, "4/%d", _prefs->cr);
    snprintf(v[4], 16, "%d", _prefs->tx_power_dbm);
    snprintf(v[5], 16, "%d", (int)radio_driver.getNoiseFloor());
    static const char* lab[6] = { "Frequency", "Bandwidth", "Spreading factor", "Coding rate", "TX power", "Noise floor" };
    static const char* unit[6] = { "MHz", "kHz", "", "", "dBm", "dBm" };
    int cw = ui.W / 2, top = ui.bodyTop();
    for (int k = 0; k < 6; k++) {
      int cx = (k % 2) * cw + 6;
      int t = top + (k / 2) * 31;
      ui.font(u8g2_font_helvR08_tf);
      ui.text(cx, t + 9, lab[k]);
      ui.font(u8g2_font_helvB12_tf);
      ui.text(cx, t + 24, v[k]);
      int vw = ui.width(v[k]);
      if (unit[k][0]) { ui.font(u8g2_font_helvR08_tf); ui.text(cx + vw + 3, t + 24, unit[k]); }
      if (k / 2 < 2) ui.hdots((k % 2) * cw + 4, (k % 2) * cw + cw - 4, t + 29);
    }
    ui.vdots(cw, top + 2, ui.bodyBottom() - 1);
  }

  void pageBluetooth(EinkUI& ui) {
    bool on = _task->isBluetoothEnabled();
    int top = ui.bodyTop();
    int x = 22, y = top + 32;
    if (on) {
      ui.fillRound(x, y, 47, 25, 12);
      ui.disc(x + 34, y + 12, 8, true);
    } else {
      ui.fillRound(x, y, 47, 25, 12);
      ui.fillRound(x + 2, y + 2, 43, 21, 10, true);
      ui.disc(x + 12, y + 12, 8);
    }
    ui.font(u8g2_font_helvB14_tf);
    ui.text(86, top + 34, on ? "Bluetooth on" : "Bluetooth off");
    char buf[24];
    ui.font(u8g2_font_helvR10_tf);
    if (the_mesh.getBLEPin()) { snprintf(buf, sizeof(buf), "Pin  %lu", (unsigned long)the_mesh.getBLEPin()); ui.text(86, top + 52, buf); }
    ui.font(u8g2_font_helvR08_tf);
    ui.text(86, top + 68, !on ? "Phone can't connect" : _task->hasConnection() ? "Phone connected" : "Waiting for phone");
  }

  void pageAdvert(EinkUI& ui) {
    int cx = 46, cy = (ui.bodyTop() + ui.bodyBottom()) / 2;
    ui.disc(cx, cy, 4);
    for (int r = 12; r <= 28; r += 8) { ui.arc(cx, cy, r, -40, 40); ui.arc(cx, cy, r, 140, 220); }
    int top = ui.bodyTop();
    ui.font(u8g2_font_helvB14_tf);
    ui.text(92, top + 30, "Send advert");
    ui.font(u8g2_font_helvR08_tf);
    ui.text(92, top + 46, "Lets nearby nodes and the");
    ui.text(92, top + 57, "map know you're here");
    if (_last_advert) {
      char buf[32], t[12];
      if (_last_advert >= UI_TIME_VALID) EinkUI::fmtClock(t, sizeof(t), _last_advert, tz());
      else EinkUI::fmtAge(t, sizeof(t), now() - _last_advert);
      snprintf(buf, sizeof(buf), "Last sent %s", t);
      ui.font(u8g2_font_helvB08_tf);
      ui.text(92, top + 76, buf);
    }
  }

  void pageDiscover(EinkUI& ui) {
#if UI_DISCOVER_SCREEN
    int top = ui.bodyTop();
    if (_disc_count == 0) {
      ui.font(u8g2_font_helvR10_tf);
      bool waiting = _disc_time && millis() - _disc_time < 6000;
      ui.textC(ui.W / 2, (top + ui.bodyBottom()) / 2 + 4,
               waiting ? "Scanning..." : _disc_time ? "No repeaters answered" : "Hold to scan for repeaters");
      return;
    }
    ui.font(u8g2_font_helvR08_tf);
    ui.text(3, top + 8, "name");
    ui.textR(ui.W - 3, top + 8, "snr in / out");
    ui.hdots(3, ui.W - 3, top + 12);
    int y = top + 14;
    for (uint32_t i = 0; i < _disc_count && y + 13 <= ui.bodyBottom() + 2; i++, y += 15) {
      DiscoveredNode& a = _disc[i];
      char name[33], snr[24];
      if (a.name[0]) EinkUI::sanitize(name, sizeof(name), a.name, strlen(a.name));
      else mesh::Utils::toHex(name, a.pubkey_prefix, 4);
      snprintf(snr, sizeof(snr), "%.1f / %.1f", a.snr_in, a.snr_out);
      ui.font(u8g2_font_helvB10_tf);
      int sw = ui.width(snr);
      ui.text(ui.W - 3 - sw, y + 11, snr);
      ui.font(u8g2_font_helvR10_tf);
      while (name[0] && ui.width(name) > ui.W - sw - 12) name[strlen(name) - 1] = 0;
      ui.text(3, y + 11, name);
    }
#endif
  }

  void pageSettings(EinkUI& ui) {
    int top = ui.bodyTop() - 1;
    int first = _edit && _edit_item > 5 ? _edit_item - 5 : 0;
    char lab[24], val[33];
    for (int k = 0; k < 6 && first + k < SETTINGS_COUNT; k++) {
      int i = first + k, y = top + k * 15;
      settingLabel(itemAt(i), lab, val, sizeof(val));
      char v[33];
      EinkUI::sanitize(v, sizeof(v), val, strlen(val));
      bool sel = _edit && i == _edit_item;
      if (sel) { ui.fill(1, y, ui.W - 2, 15); ui.inverse(); }
      ui.font(u8g2_font_helvR10_tf);
      ui.text(4, y + 12, lab);
      ui.font(u8g2_font_helvB10_tf);
      ui.textR(ui.W - 5, y + 12, v);
      if (sel) ui.normal();
    }
  }

  void pagePower(EinkUI& ui) {
    int cx = 46, cy = (ui.bodyTop() + ui.bodyBottom()) / 2, r = 18;
    ui.arc(cx, cy, r, -60, 240, 3);
    ui.fill(cx - 1, cy - r - 4, 3, r + 2);
    int top = ui.bodyTop();
    ui.font(u8g2_font_helvB14_tf);
    ui.text(92, top + 34, _shutdown_init ? "Powering off" : "Power off");
    ui.font(u8g2_font_helvR08_tf);
    ui.text(92, top + 50, "Radio and screen sleep.");
    ui.text(92, top + 61, "Press reset to wake.");
  }

public:
  NativeHomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs) { _last_input = millis(); }

  bool isOnBoard() const { return _page == BOARD; }
  bool isOnMap() const { return _page == MAP; }

  // called for every incoming message; the map keeps the latest route until the next one
  void recordRoute(const char* sender, bool has_pos, int32_t lat, int32_t lon, const mesh::Packet* pkt, const char* msg) {
    _map.collect();
    bool flood = pkt->isRouteFlood();
    int hs = flood ? pkt->getPathHashSize() : 1;
    int hops = flood ? pkt->getPathHashCount() : 0;
    _map.setRoute(sender, has_pos, lat, lon, pkt->path, hs, hops, !flood, msg, _prefs->node_lat, _prefs->node_lon);
    if (_page == MAP) display.forceFullRefresh();
  }

#if UI_DISCOVER_SCREEN
  bool sendDiscoverRequest() {
    uint8_t cmd[6];
    cmd[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1;
    cmd[1] = 0xFF;
    the_mesh.getRNG()->random((uint8_t*)&_disc_tag, 4);
    memcpy(&cmd[2], &_disc_tag, 4);
    _disc_count = 0;
    mesh::Packet* req = the_mesh.createControlData(cmd, sizeof(cmd));
    if (req) { the_mesh.sendZeroHop(req); _disc_time = millis(); return true; }
    return false;
  }

  void handleDiscoverResponse(const mesh::Packet* packet) {
    if (_disc_count < DISCOVERED_NODES_TABLE_SIZE && memcmp(&packet->payload[2], &_disc_tag, 4) == 0) {
      auto d = &_disc[_disc_count++];
      memcpy(d->pubkey_prefix, &packet->payload[6], 8);
      d->type = packet->payload[0] & 0xF;
      d->snr_out = ((int8_t)packet->payload[1]) / 4.0;
      d->snr_in = radio_driver.getLastSNR();
      ContactInfo* c = the_mesh.lookupContactByPubKey(&packet->payload[6], 8);
      if (c) strncpy(d->name, c->name, 32); else d->name[0] = 0;
      if (_page == DISCOVER) _task->gotoHomeScreen();  // redraw
    }
  }
#endif

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) _task->shutdown();
    uint32_t idle = UiSettings::idleMillis(S().idle);
    if (idle && _page != MAP && millis() - _last_input > idle) {
      MessageBoard* mb = _task->getMessageBoard();
      bool changed = false;
      if (_edit) { _edit = false; S().save(); changed = true; }
      if (_page != BOARD) { goPage(BOARD); changed = true; }
      if (mb && mb->isScrolled()) { mb->resetScroll(); changed = true; }
      if (changed) _task->gotoHomeScreen();
      _last_input = millis();
    }
  }

  int render(DisplayDriver& display) override {
    EinkUI& ui = EinkUI::get();
    UiSettings& s = S();
    bool portrait = _page == MAP && s.map_portrait;
    ui.begin((GxEPDDisplay&)display, uiRotation(s, portrait), s.dark);
    bool bt = _task->hasConnection();

    if (_page == BOARD) {
      MessageBoard* mb = _task->getMessageBoard();
      ChannelDetails tmp;
      if (mb) mb->render(ui, channelName(s.board_channel, tmp), now(), tz(), batt(), bt, s.text_large,
                         UiSettings::nameLen(s.name_len));
      return 15000;
    }

    static const char* titles[COUNT] = { "", "Map", "Status", "Recently heard", "Radio", "Bluetooth",
                                         "Advert", "Repeaters nearby", "Settings", "Power" };
    const char* title = titles[_page];
    if (_page == SETTINGS && _edit) title = "Settings  editing";
    if (_page != MAP) ui.header(title, now(), tz(), batt(), bt);

    const char* hint = "click: next   double: back";
    int dot = _page;
    switch (_page) {
      case STATUS: pageStatus(ui); break;
      case HEARD: pageHeard(ui); break;
      case MAP:
        _map.render(ui, _prefs->node_lat, _prefs->node_lon, 0, false, UiSettings::nameLen(s.name_len));
        hint = portrait ? "hold: landscape" : "hold: portrait";
        if (_prefs->node_lat == 0 && _prefs->node_lon == 0) hint = portrait ? "no location set" : "hold: portrait   no location set";
        if (_map.hasRoute()) {
          static char fm[160];
          snprintf(fm, sizeof(fm), "%s", _map.routeMessage());
          ui.font(u8g2_font_helvR08_tf);
          fitText(ui, fm, ui.W - 6);
          hint = fm;
          dot = -1;
        }
        ui.header(title, now(), tz(), batt(), bt);   // after the map so nothing draws over it
        break;
      case RADIO: pageRadio(ui); break;
      case BLUETOOTH: pageBluetooth(ui); hint = _task->isBluetoothEnabled() ? "hold: turn bluetooth off" : "hold: turn bluetooth on"; break;
      case ADVERT: pageAdvert(ui); hint = "hold: send advert"; break;
      case DISCOVER: pageDiscover(ui); hint = "hold: scan"; break;
      case SETTINGS:
        pageSettings(ui);
        hint = _edit ? "click: next item   double: change   hold: done" : "hold: edit settings";
        if (_edit) dot = -1;
        break;
      case POWER: pagePower(ui); hint = "hold: power off"; break;
    }
    ui.footer(hint, dot, COUNT);
    return _page == STATUS ? 15000 : _page == DISCOVER ? 3000 : 30000;
  }

  bool handleInput(char c) override {
    _last_input = millis();
    UiSettings& s = S();

    if (_page == SETTINGS && _edit) {
      if (c == KEY_NEXT || c == KEY_RIGHT) { _edit_item = (_edit_item + 1) % SETTINGS_COUNT; return true; }
      if (c == KEY_PREV || c == KEY_LEFT) { changeSetting(itemAt(_edit_item)); return true; }
      if (c == KEY_ENTER) { _edit = false; s.save(); return true; }
      return false;
    }

    if (_page == BOARD) {
      MessageBoard* mb = _task->getMessageBoard();
      if (mb && (c == KEY_PREV || c == KEY_LEFT)) { mb->scrollOlder(); return true; }
      if (mb && (c == KEY_NEXT || c == KEY_RIGHT) && mb->scrollNewer()) return true;
      if (c == KEY_ENTER) {
        s.text_large ^= 1;
        s.save();
        display.forceFullRefresh();
        _task->showAlert(s.text_large ? "Large text" : "Small text", 800);
        return true;
      }
    }

    if (c == KEY_NEXT || c == KEY_RIGHT) { goPage(_page + 1); return true; }
    if (c == KEY_PREV || c == KEY_LEFT) { goPage(_page - 1); return true; }

    if (c == KEY_ENTER) {
      switch (_page) {
        case MAP:   // flip between landscape and portrait, keeping any route on screen
          s.map_portrait ^= 1;
          s.save();
          display.forceFullRefresh();
          return true;
        case BLUETOOTH:
          if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth();
          return true;
        case ADVERT:
          _task->notify(UIEventType::ack);
          if (the_mesh.advert()) { _last_advert = now(); _task->showAlert("Advert sent", 1200); }
          else _task->showAlert("Advert failed", 1200);
          return true;
#if UI_DISCOVER_SCREEN
        case DISCOVER:
          if (millis() > _disc_time + 5000 || _disc_time == 0) { sendDiscoverRequest(); _task->showAlert("Scanning", 800); }
          return true;
#endif
        case SETTINGS: _edit = true; _edit_item = 0; return true;
        case POWER: _shutdown_init = true; return true;
        default: break;
      }
    }
    return false;
  }
};

#endif
