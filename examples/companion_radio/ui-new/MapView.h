#pragma once

// Offline map page: land (dotted), water (white), rivers, border, nodes with
// shared locations (clustered), and your own position. Map data in MapData.h.

#ifdef UI_MESSAGE_BOARD

#include "EinkUI.h"
#include "MapData.h"

#ifndef MAP_MAX_NODES
  #define MAP_MAX_NODES 360
#endif
#define MAP_MAX_EDGES  1600
#define MAP_MAX_XINGS  96
#define MAP_MAX_LABELS 40

class MapView {
  struct Node { int32_t lat, lon; char name[12]; };          // 1e-6 degrees
  struct Edge { int16_t x0, y0, x1, y1; } __attribute__((packed));
  struct Rect { int16_t x0, y0, x1, y1; };

  Node _nodes[MAP_MAX_NODES];
  int _count = 0;

  // clustering scratch
  int32_t _csx[MAP_MAX_NODES], _csy[MAP_MAX_NODES];
  int16_t _cn[MAP_MAX_NODES], _cfirst[MAP_MAX_NODES];

  // current view
  int32_t _lat0u, _lon0u;   // view centre, 1e-4 degree units
  float _ppu_x, _ppu_y;     // pixels per 1e-4 degree
  float _kmpp;
  int _W, _top, _bot, _cx, _cy;

  Rect _taken[MAP_MAX_LABELS];
  int _ntaken = 0;

  static int32_t rd32(const uint8_t* p) { int32_t v; memcpy(&v, p, 4); return v; }
  static int16_t rd16(const uint8_t* p) { int16_t v; memcpy(&v, p, 2); return v; }
  static uint16_t rdu16(const uint8_t* p) { uint16_t v; memcpy(&v, p, 2); return v; }

  int32_t px(int32_t lonu) const {
    float v = _cx + (lonu - _lon0u) * _ppu_x;
    return v < -1e6f ? -1000000 : v > 1e6f ? 1000000 : (int32_t)lroundf(v);
  }
  int32_t py(int32_t latu) const {
    float v = _cy - (latu - _lat0u) * _ppu_y;
    return v < -1e6f ? -1000000 : v > 1e6f ? 1000000 : (int32_t)lroundf(v);
  }

  // Liang-Barsky clip of a segment to [x0,x1]x[y0,y1]; false when fully outside
  static bool clip(float& ax, float& ay, float& bx, float& by, float x0, float y0, float x1, float y1) {
    float t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
    float p[4] = { -dx, dx, -dy, dy }, q[4] = { ax - x0, x1 - ax, ay - y0, y1 - ay };
    for (int i = 0; i < 4; i++) {
      if (p[i] == 0) { if (q[i] < 0) return false; continue; }
      float r = q[i] / p[i];
      if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
      else { if (r < t0) return false; if (r < t1) t1 = r; }
    }
    float nax = ax + t0 * dx, nay = ay + t0 * dy, nbx = ax + t1 * dx, nby = ay + t1 * dy;
    ax = nax; ay = nay; bx = nbx; by = nby;
    return true;
  }

  void pushEdge(Edge* edges, int& n, bool& overflow, float x0, float y0, float x1, float y1) {
    int16_t a = (int16_t)lroundf(y0), b = (int16_t)lroundf(y1);
    if (a == b) return;
    if (!edges || n >= MAP_MAX_EDGES) { overflow = true; return; }
    edges[n++] = { (int16_t)lroundf(x0), a, (int16_t)lroundf(x1), b };
  }

  // Add a land edge for even-odd filling. Clipped to the visible rows; the parts
  // left or right of the screen become vertical edges just outside it, which keeps
  // the crossing parity of every visible pixel correct.
  void addFillEdge(Edge* edges, int& n, bool& overflow, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    float x0 = ax, y0 = ay, x1 = bx, y1 = by;
    float ylo = _top - 1, yhi = _bot + 1;
    if (!clip(x0, y0, x1, y1, -1e7f, ylo, 1e7f, yhi)) return;
    const float L = -2, R = _W + 1;
    if (x0 > x1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x1 <= L) { pushEdge(edges, n, overflow, L, y0, L, y1); return; }
    if (x0 >= R) { pushEdge(edges, n, overflow, R, y0, R, y1); return; }
    float ya = y0, yb = y1;
    if (x0 < L) { ya = y0 + (y1 - y0) * (L - x0) / (x1 - x0); pushEdge(edges, n, overflow, L, y0, L, ya); x0 = L; }
    if (x1 > R) { yb = y0 + (y1 - y0) * (R - x0) / (x1 - x0); pushEdge(edges, n, overflow, R, yb, R, y1); x1 = R; }
    pushEdge(edges, n, overflow, x0, ya, x1, yb);
  }

  void drawClipped(EinkUI& ui, int32_t ax, int32_t ay, int32_t bx, int32_t by, bool dashed, int& phase) {
    float x0 = ax, y0 = ay, x1 = bx, y1 = by;
    if (!clip(x0, y0, x1, y1, 0, _top, _W - 1, _bot)) return;
#ifdef MAP_SIM
    if (getenv("NOLINES")) return;
#endif
    int a = lroundf(x0), b = lroundf(y0), c = lroundf(x1), d = lroundf(y1);
    if (dashed) dashedLine(ui, a, b, c, d, phase);
    else ui.g->drawLine(a, b, c, d, ui.ink);
  }

  bool isFree(const Rect& r) const {
    for (int i = 0; i < _ntaken; i++) {
      const Rect& t = _taken[i];
      if (!(r.x1 < t.x0 || r.x0 > t.x1 || r.y1 < t.y0 || r.y0 > t.y1)) return false;
    }
    return true;
  }
  void take(int x0, int y0, int x1, int y1) {
    if (_ntaken < MAP_MAX_LABELS) _taken[_ntaken++] = { (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1 };
  }

  void dashedLine(EinkUI& ui, int x0, int y0, int x1, int y1, int& phase) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      if (((phase++ >> 1) & 1) == 0 && x0 >= 0 && x0 < _W && y0 >= _top && y0 <= _bot) ui.g->drawPixel(x0, y0, ui.ink);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // Draw one level: dotted land fill, coast outline, rivers, border
  void drawLevel(EinkUI& ui, const MapLevel& lv) {
    // view bounds in data units, with margin
    int32_t half_w = (int32_t)((_W / 2 + 4) / _ppu_x);
    int32_t half_h = (int32_t)(((_bot - _top) / 2 + 4) / _ppu_y);
    int32_t vminx = _lon0u - half_w, vmaxx = _lon0u + half_w;
    int32_t vminy = _lat0u - half_h, vmaxy = _lat0u + half_h;

    Edge* edges = (Edge*)malloc(sizeof(Edge) * MAP_MAX_EDGES);
    int nedges = 0;
    bool overflow = (edges == nullptr);
    int phase = 0;

    const uint8_t* p = lv.data;
    const uint8_t* end = lv.data + lv.len;
    while (p + 27 <= end) {
      uint8_t kind = p[0];
      uint16_t n = rdu16(p + 1);
      int32_t bx0 = rd32(p + 3), by0 = rd32(p + 7), bx1 = rd32(p + 11), by1 = rd32(p + 15);
      int32_t x = rd32(p + 19), y = rd32(p + 23);
      const uint8_t* q = p + 27;
      p = q + (n - 1) * 4;
      if (p > end) break;
      bool visible = !(bx1 < vminx || bx0 > vmaxx || by1 < vminy || by0 > vmaxy);
      // land rings left of the view still matter for even-odd filling; others can be skipped
      bool for_fill = kind == 0 && !(by1 < vminy || by0 > vmaxy || bx0 > vmaxx);
      if (!visible && !for_fill) continue;

      int32_t sx = px(x), sy = py(y);
      for (int i = 1; i < n; i++, q += 4) {
        int32_t nx = x + rd16(q), ny = y + rd16(q + 2);
        int32_t ex = px(nx), ey = py(ny);
        // skip points closer than 2px to the last drawn one (keeps the ring's last point)
        if (abs(ex - sx) + abs(ey - sy) < 2 && i < n - 1) { x = nx; y = ny; continue; }
        if (kind == 0) {
          // edges lying on the data clip boundary are fill only, not coastline
          bool on_clip = (x == lv.minx && nx == lv.minx) || (x == lv.maxx && nx == lv.maxx) ||
                         (y == lv.miny && ny == lv.miny) || (y == lv.maxy && ny == lv.maxy);
          if (!on_clip && visible) drawClipped(ui, sx, sy, ex, ey, false, phase);
          if (!overflow) addFillEdge(edges, nedges, overflow, sx, sy, ex, ey);
        } else {
          drawClipped(ui, sx, sy, ex, ey, kind == 2, phase);
        }
        sx = ex; sy = ey; x = nx; y = ny;
      }
    }

#ifdef MAP_SIM
    printf("  edges %d overflow %d\n", nedges, overflow);
#endif
    // even-odd scanline fill with a 1-in-9 dot pattern
    bool do_fill = true;
#ifdef MAP_SIM
    if (getenv("NOFILL")) do_fill = false;
#endif
    if (do_fill && !overflow && edges) {
      int16_t xs[MAP_MAX_XINGS];
      for (int yy = _top - (_top % 3) + 3; yy <= _bot; yy += 3) {
        int nx = 0;
        float fy = yy + 0.5f;
        for (int i = 0; i < nedges && nx < MAP_MAX_XINGS; i++) {
          const Edge& e = edges[i];
          int16_t ya = e.y0 < e.y1 ? e.y0 : e.y1, yb = e.y0 < e.y1 ? e.y1 : e.y0;
          if (fy < ya || fy >= yb) continue;
          float xv = e.x0 + (fy - e.y0) * (float)(e.x1 - e.x0) / (float)(e.y1 - e.y0);
          xs[nx++] = (int16_t)lroundf(xv);
        }
        for (int i = 1; i < nx; i++) {  // insertion sort
          int16_t v = xs[i]; int j = i - 1;
          while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
          xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
          int a = xs[i] < 0 ? 0 : xs[i], b = xs[i + 1] >= _W ? _W - 1 : xs[i + 1];
          int start = a + ((3 - (a % 3)) % 3);
          for (int xx = start; xx <= b; xx += 3) ui.g->drawPixel(xx, yy, ui.ink);
        }
      }
    }
    if (edges) free(edges);
  }

public:
  static const int ZOOM_COUNT = 7;
  static int zoomKm(int z) { static const int v[] = { 0, 200, 100, 50, 20, 10, 5 }; return v[z % ZOOM_COUNT]; }
  int count() const { return _count; }

  // Gather contacts that share a location
  void addNode(int32_t lat_e6, int32_t lon_e6, const char* name) {
    if (_count >= MAP_MAX_NODES) return;
    Node& n = _nodes[_count++];
    n.lat = lat_e6; n.lon = lon_e6;
    EinkUI::sanitize(n.name, sizeof(n.name), name, strlen(name));
    EinkUI::truncChars(n.name, 5);
  }

  void collect() {
#ifndef MAP_SIM
    _count = 0;
    ContactsIterator it = the_mesh.startContactsIterator();
    ContactInfo c;
    while (_count < MAP_MAX_NODES && it.hasNext(&the_mesh, c)) {
      if (c.gps_lat == 0 && c.gps_lon == 0) continue;
      Node& n = _nodes[_count++];
      n.lat = c.gps_lat;
      n.lon = c.gps_lon;
      EinkUI::sanitize(n.name, sizeof(n.name), c.name, strlen(c.name));
      EinkUI::truncChars(n.name, 5);
    }
#endif
  }

  void render(EinkUI& ui, double me_lat, double me_lon, int zoom, bool zoom_mode, int name_chars) {
    _W = ui.W;
    _top = UI_HEADER_H;
    _bot = ui.H - UI_FOOTER_H - 1;
    _cx = _W / 2;
    _cy = (_top + _bot) / 2;
    bool have_me = !(me_lat == 0 && me_lon == 0);

    // view centre and size
    double clat, clon, height_km;
    double k;
    if (zoom == 0) {
      double a = 90, b = -90, c = 180, d = -180;
      for (int i = 0; i < _count; i++) {
        double la = _nodes[i].lat * 1e-6, lo = _nodes[i].lon * 1e-6;
        a = fmin(a, la); b = fmax(b, la); c = fmin(c, lo); d = fmax(d, lo);
      }
      if (have_me) { a = fmin(a, me_lat); b = fmax(b, me_lat); c = fmin(c, me_lon); d = fmax(d, me_lon); }
      if (a > b) { a = b = 49.25; c = d = -123.0; }   // nothing known yet
      clat = (a + b) / 2; clon = (c + d) / 2;
      k = cos(clat * DEG_TO_RAD);
      double h = (b - a) * 110.57;
      double w = (d - c) * 111.32 * k * (_bot - _top) / (double)_W;
      height_km = fmax(fmax(h, w) * 1.15, 5.0);
    } else {
      if (have_me) { clat = me_lat; clon = me_lon; }
      else if (_count > 0) {
        double sa = 0, so = 0;
        for (int i = 0; i < _count; i++) { sa += _nodes[i].lat * 1e-6; so += _nodes[i].lon * 1e-6; }
        clat = sa / _count; clon = so / _count;
      } else { clat = 49.25; clon = -123.0; }
      k = cos(clat * DEG_TO_RAD);
      height_km = zoomKm(zoom);
    }
    _kmpp = height_km / (_bot - _top);
    _lat0u = lround(clat * 1e4);
    _lon0u = lround(clon * 1e4);
    _ppu_x = (float)(111.32 * k / _kmpp * 1e-4);
    _ppu_y = (float)(110.57 / _kmpp * 1e-4);
    ui.crc4(_lat0u, _lon0u, (int)(_kmpp * 1000), zoom * 2 + (zoom_mode ? 1 : 0));

    // pick the finest map level that suits this scale and covers the centre
    int lv = MAP_LEVEL_COUNT - 1;
    static const float max_kmpp[] = { 0.5f, 1.2f, 1e9f };
    for (int i = 0; i < MAP_LEVEL_COUNT; i++) {
      const MapLevel& L = MAP_LEVELS[i];
      if (_kmpp <= max_kmpp[i] && _lon0u >= L.minx && _lon0u <= L.maxx && _lat0u >= L.miny && _lat0u <= L.maxy) { lv = i; break; }
    }
#ifdef MAP_SIM
    printf("zoom %d count %d centre %d %d kmpp %.3f level %d\n", zoom, _count, _lat0u, _lon0u, _kmpp, lv);
#endif
    drawLevel(ui, MAP_LEVELS[lv]);

    // nodes, clustered so markers never overlap
    _ntaken = 0;
    int mex = 0, mey = 0;
    if (have_me) {
      mex = px(lround(me_lon * 1e4)); mey = py(lround(me_lat * 1e4));
      take(mex - 8, mey - 8, mex + 8, mey + 8);
    }
    int nc = 0;
    int32_t hash = 0;
    for (int i = 0; i < _count; i++) {
      int x = px(_nodes[i].lon / 100), y = py(_nodes[i].lat / 100);
      if (x < 3 || x > _W - 4 || y < _top + 4 || y > _bot - 4) continue;
      hash += x * 31 + y;
      int j = 0;
      for (; j < nc; j++) {
        if (abs(_csx[j] / _cn[j] - x) < 14 && abs(_csy[j] / _cn[j] - y) < 14) break;
      }
      if (j < nc) { _csx[j] += x; _csy[j] += y; _cn[j]++; }
      else { _csx[nc] = x; _csy[nc] = y; _cn[nc] = 1; _cfirst[nc] = i; nc++; }
    }
    for (bool merged = true; merged; ) {
      merged = false;
      for (int a = 0; a < nc && !merged; a++) {
        for (int b = a + 1; b < nc; b++) {
          if (abs(_csx[a] / _cn[a] - _csx[b] / _cn[b]) < 15 && abs(_csy[a] / _cn[a] - _csy[b] / _cn[b]) < 15) {
            _csx[a] += _csx[b]; _csy[a] += _csy[b]; _cn[a] += _cn[b];
            _csx[b] = _csx[nc - 1]; _csy[b] = _csy[nc - 1]; _cn[b] = _cn[nc - 1]; _cfirst[b] = _cfirst[nc - 1];
            nc--; merged = true; break;
          }
        }
      }
    }
    ui.crc4(nc, hash, have_me ? mex : -1, mey);

    bool labels = zoom >= 4;
    ui.font(u8g2_font_helvB08_tf);
    for (int c = 0; c < nc; c++) {
      int x = _csx[c] / _cn[c], y = _csy[c] / _cn[c];
      if (_cn[c] == 1) {
        ui.g->fillRect(x - 2, y - 2, 5, 5, ui.ink);
        take(x - 3, y - 3, x + 3, y + 3);
      } else {
        char s[6];
        snprintf(s, sizeof(s), "%d", _cn[c]);
        int tw = ui.width(s);
        int r = tw / 2 + 3 < 6 ? 6 : tw / 2 + 3;
        ui.g->fillCircle(x, y, r + 1, ui.paper);
        ui.g->fillCircle(x, y, r, ui.ink);
        ui.inverse();
        ui.u8.drawUTF8(x - tw / 2, y + 4, s);
        ui.normal();
        take(x - r, y - r, x + r, y + r);
      }
    }
    if (labels) {
      ui.font(u8g2_font_helvR08_tf);
      for (int c = 0; c < nc; c++) {
        if (_cn[c] != 1) continue;
        int x = _csx[c], y = _csy[c];
        char lab[12];
        snprintf(lab, sizeof(lab), "%s", _nodes[_cfirst[c]].name);
        EinkUI::truncChars(lab, name_chars > 0 && name_chars < 5 ? name_chars : 5);
        int tw = ui.width(lab);
        for (int side = 0; side < 2; side++) {
          int lx = side == 0 ? x + 5 : x - 5 - tw;
          Rect r = { (int16_t)(lx - 1), (int16_t)(y - 6), (int16_t)(lx + tw), (int16_t)(y + 3) };
          if (lx >= 1 && lx + tw <= _W - 2 && r.y0 > _top && isFree(r)) {
            take(r.x0, r.y0, r.x1, r.y1);
            ui.g->fillRect(r.x0, r.y0, tw + 2, 10, ui.paper);
            ui.u8.drawUTF8(lx, y + 3, lab);
            break;
          }
        }
      }
    }

    // you
    if (have_me && mey >= _top + 6 && mey <= _bot - 6 && mex >= 6 && mex <= _W - 7) {
      ui.g->fillCircle(mex, mey, 6, ui.paper);
      ui.g->drawCircle(mex, mey, 6, ui.ink);
      ui.g->drawCircle(mex, mey, 5, ui.ink);
      ui.g->fillCircle(mex, mey, 2, ui.ink);
    }

    // scale bar
    static const int steps[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500 };
    int km = 500;
    for (int s : steps) { if (s / _kmpp >= 24) { km = s; break; } }
    int L = (int)(km / _kmpp);
    char sb[12];
    snprintf(sb, sizeof(sb), "%d km", km);
    ui.font(u8g2_font_helvR08_tf);
    ui.fill(2, _bot - 10, L + 10 + ui.width(sb), 10, true);
    ui.fill(4, _bot - 4, L, 2);
    ui.fill(4, _bot - 7, 1, 5);
    ui.fill(4 + L - 1, _bot - 7, 1, 5);
    ui.text(8 + L, _bot - 1, sb);

    // zoom tag top right, inverted while zooming
    char zl[12];
    if (zoom == 0) snprintf(zl, sizeof(zl), "All");
    else snprintf(zl, sizeof(zl), "%d km", zoomKm(zoom));
    ui.font(u8g2_font_helvB08_tf);
    int tw = ui.width(zl);
    if (zoom_mode) {
      ui.fill(_W - tw - 8, _top + 2, tw + 6, 12);
      ui.inverse();
      ui.text(_W - tw - 5, _top + 11, zl);
      ui.normal();
    } else {
      ui.fill(_W - tw - 8, _top + 2, tw + 6, 12, true);
      ui.frame(_W - tw - 8, _top + 2, tw + 6, 12);
      ui.text(_W - tw - 5, _top + 11, zl);
    }

    if (_count == 0 && !have_me) {
      ui.font(u8g2_font_helvB08_tf);
      const char* m1 = "No locations yet";
      int w = ui.width(m1) + 10;
      ui.fill(_cx - w / 2, _cy - 10, w, 20, true);
      ui.frame(_cx - w / 2, _cy - 10, w, 20);
      ui.textC(_cx, _cy + 4, m1);
    }
  }
};

#endif
