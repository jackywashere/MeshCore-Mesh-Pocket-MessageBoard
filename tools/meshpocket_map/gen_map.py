"""Generate MapData.h for the MeshPocket map page.

Usage:
  1. Download the source data into a folder (default ./geo, or set MAP_SRC):
       osm_coast.json  https://github.com/simonepri/geo-maps/releases/download/v0.6.0/earth-coastlines-100m.geo.json
       ne_10m_lakes.geojson, ne_10m_rivers_lake_centerlines.geojson,
       ne_10m_rivers_north_america.geojson, ne_10m_admin_0_boundary_lines_land.geojson
         from https://github.com/nvkelso/natural-earth-vector/tree/master/geojson
  2. Edit LEVELS below for your region (lon/lat boxes, finest first).
  3. pip install shapely
  4. python gen_map.py ../../examples/companion_radio/ui-new/MapData.h

Sources: OpenStreetMap land (simonepri/geo-maps earth-coastlines-100m, ODbL),
Natural Earth 10m lakes, rivers and borders (public domain).

Format per level, a stream of records:
  uint8  kind        0 = land ring (filled, even-odd), 1 = river line, 2 = border line
  uint16 npts
  int32  minx, miny, maxx, maxy   (1e-4 degree units)
  int32  x0, y0
  int16  dx, dy  * (npts - 1)
"""
import json, math, os, struct, sys
from shapely.geometry import shape, box, Polygon, MultiPolygon, LineString, MultiLineString, GeometryCollection
from shapely.ops import unary_union, transform
from shapely import set_precision

GEO = os.environ.get("MAP_SRC", os.path.join(os.path.dirname(os.path.abspath(__file__)), "geo"))
Q = 10000  # 1e-4 degree units

# ---- REGION: edit these boxes for your area. Default: Pacific Northwest + West Coast overview.
# The firmware picks the finest level that covers the view, so keep them nested (detail first).
LEVELS = [
    # name, bbox (lon/lat), simplify tolerance km, min polygon area km2, rivers?
    ("L0", (-125.0, 46.5, -121.0, 50.3), 0.06, 0.02, True),
    ("L1", (-129.0, 45.0, -118.5, 52.5), 0.35, 0.5, True),
    ("L2", (-140.0, 32.0, -114.0, 61.0), 2.0, 15.0, False),
]

K = math.cos(math.radians(48.5))


def to_km(g):
    return transform(lambda x, y, z=None: (x * 111.32 * K, y * 110.57), g)


def to_deg(g):
    return transform(lambda x, y, z=None: (x / (111.32 * K), y / 110.57), g)


def polys(g):
    if g.is_empty:
        return []
    if isinstance(g, Polygon):
        return [g]
    if isinstance(g, (MultiPolygon, GeometryCollection)):
        return [p for x in g.geoms for p in polys(x)]
    return []


def lines(g):
    if g.is_empty:
        return []
    if isinstance(g, LineString):
        return [g]
    if isinstance(g, (MultiLineString, GeometryCollection)):
        return [p for x in g.geoms for p in lines(x)]
    return []


print("loading land...", file=sys.stderr)
from shapely.validation import make_valid
_raw = json.load(open(os.path.join(GEO, "osm_coast.json")))["geometries"][0]["coordinates"]
_outer = box(-141, 31, -113, 62)
land_parts = []
for pc in _raw:
    xs = [c[0] for c in pc[0]]; ys = [c[1] for c in pc[0]]
    if max(xs) < -141 or min(xs) > -113 or max(ys) < 31 or min(ys) > 62:
        continue
    poly = Polygon(pc[0], pc[1:])
    if not poly.is_valid:
        poly = make_valid(poly)
    land_parts.append(poly)
print("land parts", len(land_parts), file=sys.stderr)
lakes_all = [shape(f["geometry"]) for f in json.load(open(os.path.join(GEO, "ne_10m_lakes.geojson")))["features"] if f["geometry"]]
rivers_all = []
for fn in ("ne_10m_rivers_lake_centerlines", "ne_10m_rivers_north_america"):
    for f in json.load(open(os.path.join(GEO, fn + ".geojson")))["features"]:
        if f["geometry"]:
            rivers_all.append(shape(f["geometry"]))
border_all = [shape(f["geometry"]) for f in json.load(open(os.path.join(GEO, "ne_10m_admin_0_boundary_lines_land.geojson")))["features"] if f["geometry"]]


def q(v):
    return int(round(v * Q))


def densify(coords, max_deg=3.0):
    out = [coords[0]]
    for (x0, y0), (x1, y1) in zip(coords, coords[1:]):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) / max_deg) + 1
        for i in range(1, n + 1):
            out.append((x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n))
    return out


def encode(kind, coords):
    pts = [(q(x), q(y)) for x, y in densify(list(coords))]
    ded = [pts[0]]
    for p in pts[1:]:
        if p != ded[-1]:
            ded.append(p)
    if len(ded) < 2 or (kind == 0 and len(ded) < 4):
        return b"", 0
    xs = [p[0] for p in ded]; ys = [p[1] for p in ded]
    b = struct.pack("<BH4i2i", kind, len(ded), min(xs), min(ys), max(xs), max(ys), ded[0][0], ded[0][1])
    for (ax, ay), (bx, by) in zip(ded, ded[1:]):
        b += struct.pack("<hh", bx - ax, by - ay)
    return b, len(ded)


out_levels = []
for name, bb, tol, min_area, with_rivers in LEVELS:
    clip = box(*bb)
    print(name, "clipping land", file=sys.stderr)
    pieces = []
    for lp in land_parts:
        if not lp.intersects(clip):
            continue
        try:
            pieces.append(lp.intersection(clip))
        except Exception:
            pieces.append(lp.buffer(0).intersection(clip))
    land = unary_union(pieces)
    lakes = unary_union([make_valid(l) for l in lakes_all if l.intersects(clip)]).intersection(clip)
    land = land.difference(lakes)
    data = b""; npts = 0; nrec = 0
    land_km = to_km(land).simplify(tol, preserve_topology=True)
    for p in polys(land_km):
        if p.area < min_area:
            continue
        p = to_deg(p)
        for ring in [p.exterior] + list(p.interiors):
            r_km = to_km(Polygon(ring))
            if r_km.area < min_area:
                continue
            b, n = encode(0, ring.coords)
            data += b; npts += n; nrec += 1 if n else 0
    if with_rivers:
        for r in rivers_all:
            if not r.intersects(clip):
                continue
            for l in lines(to_deg(to_km(r.intersection(clip)).simplify(tol))):
                b, n = encode(1, l.coords)
                data += b; npts += n; nrec += 1 if n else 0
    for r in border_all:
        if not r.intersects(clip):
            continue
        for l in lines(to_deg(to_km(r.intersection(clip)).simplify(tol))):
            b, n = encode(2, l.coords)
            data += b; npts += n; nrec += 1 if n else 0
    print(name, "records", nrec, "points", npts, "bytes", len(data), file=sys.stderr)
    out_levels.append((name, bb, data))

h = ["// Generated by gen_map.py. Map data: (c) OpenStreetMap contributors (ODbL), Natural Earth (public domain).",
     "#pragma once", "#include <stdint.h>", "",
     "struct MapLevel { int32_t minx, miny, maxx, maxy; const uint8_t* data; uint32_t len; };", ""]
for name, bb, data in out_levels:
    h.append(f"static const uint8_t MAP_{name}[{len(data)}] = {{")
    for i in range(0, len(data), 24):
        h.append("  " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    h.append("};")
h.append(f"static const MapLevel MAP_LEVELS[{len(out_levels)}] = {{")
for name, bb, data in out_levels:
    h.append(f"  {{ {q(bb[0])}, {q(bb[1])}, {q(bb[2])}, {q(bb[3])}, MAP_{name}, sizeof(MAP_{name}) }},")
h.append("};")
h.append(f"#define MAP_LEVEL_COUNT {len(out_levels)}")
open(sys.argv[1], "w").write("\n".join(h) + "\n")
print("total bytes", sum(len(d) for _, _, d in out_levels), file=sys.stderr)
