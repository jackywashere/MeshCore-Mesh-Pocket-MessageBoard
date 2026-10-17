#pragma once

// On-device display settings for the MeshPocket UI, saved to the internal filesystem.

#ifdef UI_MESSAGE_BOARD

#include <Arduino.h>
#include <InternalFileSystem.h>

#define UI_SETTINGS_FILE "/eink_ui"
#define UI_SETTINGS_MAGIC 0xB7

struct UiSettings {
  uint8_t magic = UI_SETTINGS_MAGIC;
  uint8_t version = 2;
  uint8_t board_channel = 0;  // channel index shown on the board
  uint8_t text_large = 0;     // board text size
  uint8_t name_len = 1;       // index into nameLenOptions
  uint8_t flip = 0;           // rotate everything 180 degrees
  uint8_t dark = 0;           // white on black
  uint8_t idle = 1;           // index into idleOptions
  uint8_t refresh = 1;        // index into refreshOptions
  uint8_t map_portrait = 0;   // map page orientation (landscape by default)
  uint8_t map_zoom = 0;       // last map zoom level
  uint8_t reserved[5] = { 0 };

  static const int NAME_LEN_COUNT = 4;
  static int nameLen(int i)       { static const int v[] = { 3, 5, 8, 0 }; return v[i % NAME_LEN_COUNT]; }
  static const int IDLE_COUNT = 4;
  static uint32_t idleMillis(int i) { static const uint32_t v[] = { 30000, 60000, 300000, 0 }; return v[i % IDLE_COUNT]; }
  static const char* idleLabel(int i) { static const char* v[] = { "30 sec", "1 min", "5 min", "Never" }; return v[i % IDLE_COUNT]; }
  static const int REFRESH_COUNT = 4;
  static int refreshEvery(int i)  { static const int v[] = { 10, 30, 60, -1 }; return v[i % REFRESH_COUNT]; }

  void load() {
    using namespace Adafruit_LittleFS_Namespace;
    File f = InternalFS.open(UI_SETTINGS_FILE, FILE_O_READ);
    if (f) {
      UiSettings tmp;
      if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) && tmp.magic == UI_SETTINGS_MAGIC) {
        *this = tmp;
        if (version < 2) { map_portrait = 0; version = 2; }   // map now defaults to landscape
      }
      f.close();
    }
  }

  void save() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.remove(UI_SETTINGS_FILE);
    File f = InternalFS.open(UI_SETTINGS_FILE, FILE_O_WRITE);
    if (f) {
      f.write((const uint8_t*)this, sizeof(*this));
      f.close();
    }
  }
};

#endif
