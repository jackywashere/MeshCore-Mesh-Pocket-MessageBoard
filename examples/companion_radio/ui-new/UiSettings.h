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
  uint8_t tz_mode = 0;        // 0 = Pacific (auto daylight saving), see tzLabel()
  uint8_t reserved[4] = { 0 };

  static const int NAME_LEN_COUNT = 4;
  static int nameLen(int i)       { static const int v[] = { 3, 5, 8, 0 }; return v[i % NAME_LEN_COUNT]; }
  static const int IDLE_COUNT = 4;
  static uint32_t idleMillis(int i) { static const uint32_t v[] = { 30000, 60000, 300000, 0 }; return v[i % IDLE_COUNT]; }
  static const char* idleLabel(int i) { static const char* v[] = { "30 sec", "1 min", "5 min", "Never" }; return v[i % IDLE_COUNT]; }
  // ---- time zone: North American zones follow daylight saving automatically
  static const int TZ_COUNT = 9;
  static const char* tzLabel(int i) {
    static const char* v[] = { "Pacific", "Mountain", "Arizona", "Central", "Saskatchewan", "Eastern", "Atlantic", "UTC", "From app" };
    return v[i % TZ_COUNT];
  }
  static int32_t daysFromCivil(int y, int m, int d) {   // days since 1970-01-01
    y -= m <= 2;
    int32_t era = (y >= 0 ? y : y - 399) / 400;
    uint32_t yoe = y - era * 400;
    uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
  }
  static int32_t nthSunday(int y, int m, int n) {      // day number of the nth Sunday of a month
    int32_t first = daysFromCivil(y, m, 1);
    int wd = (first + 4) % 7;                            // 0 = Sunday
    return first + ((7 - wd) % 7) + 7 * (n - 1);
  }
  // US/Canada rule: 2nd Sunday of March 2:00 local to 1st Sunday of November 2:00 local
  static bool usDst(uint32_t utc, int std_hours) {
    int32_t days = (int32_t)(utc / 86400);
    int y = 1970 + days / 366;                           // find the year
    while (daysFromCivil(y + 1, 1, 1) <= days) y++;
    int32_t start = nthSunday(y, 3, 2) * 86400 + (2 - std_hours) * 3600;
    int32_t end   = nthSunday(y, 11, 1) * 86400 + (1 - std_hours) * 3600;
    return (int32_t)utc >= start && (int32_t)utc < end;
  }
  int tzHours(uint32_t utc, int app_offset) const {
    static const int8_t std_off[] = { -8, -7, -7, -6, -6, -5, -4, 0, 0 };
    static const bool dst[] = { true, true, false, true, false, true, true, false, false };
    int m = tz_mode % TZ_COUNT;
    if (m == 8) return app_offset;
    return std_off[m] + (dst[m] && usDst(utc, std_off[m]) ? 1 : 0);
  }

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
