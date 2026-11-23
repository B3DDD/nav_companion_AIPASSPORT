#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查一个 GPX 文件能否被导航副屏正确使用。

用法:
    python3 tools/gpx_inspect.py 路线.gpx

输出:
    - 轨迹点数、路线总长
    - 命名航点（CP）列表，以及每个点被判为 CP 还是"起点/终点（已排除）"
    - 海拔范围与总爬升（3m 滞回，与 App 一致）

判断规则与 App 的 NavEngine.planCheckpoints 保持一致：
    1. 名字含 起终点/起点/终点/出发/完赛/start/finish 的排除；
    2. 沿路线里程距起点或终点 <= 100m 的排除。
"""
import math
import re
import sys
import xml.etree.ElementTree as ET

EARTH_R = 6371000.0
START_END_MARGIN_M = 100.0
ASCENT_HYSTERESIS_M = 3.0
START_FINISH_WORDS = ("起终点", "起点", "终点", "出发", "完赛", "start", "finish")


def haversine(a, b):
    la1, lo1 = math.radians(a[0]), math.radians(a[1])
    la2, lo2 = math.radians(b[0]), math.radians(b[1])
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * EARTH_R * math.asin(math.sqrt(h))


def local(tag):
    return tag.split("}")[-1] if "}" in tag else tag


def parse(path):
    track, waypoints = [], []
    for _, el in ET.iterparse(path, events=("end",)):
        name = local(el.tag)
        if name in ("trkpt", "rtept"):
            lat, lon = el.get("lat"), el.get("lon")
            if lat and lon:
                ele = el.find("./{*}ele")
                if ele is None:
                    ele = el.find("ele")
                track.append((float(lat), float(lon), float(ele.text) if ele is not None and ele.text else None))
        elif name == "wpt":
            lat, lon = el.get("lat"), el.get("lon")
            if lat and lon:
                nm = el.find("./{*}name")
                if nm is None:
                    nm = el.find("name")
                waypoints.append(((nm.text or "").strip() if nm is not None else "", float(lat), float(lon)))
    return track, waypoints


def total_ascent(elevations):
    ascent, low = 0.0, None
    for e in elevations:
        if e is None:
            continue
        if low is None:
            low = e
        elif e > low + ASCENT_HYSTERESIS_M:
            ascent += e - low
            low = e
        elif e < low:
            low = e
    return ascent


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    track, waypoints = parse(path)
    if len(track) < 2:
        print("轨迹点不足 2 个，无法使用。文件可能只有航点没有轨迹。")
        return 2

    cum = [0.0]
    for i in range(1, len(track)):
        cum.append(cum[-1] + haversine(track[i - 1][:2], track[i][:2]))
    total = cum[-1]
    elevations = [p[2] for p in track if p[2] is not None]

    print(f"文件      : {path}")
    print(f"轨迹点    : {len(track)}")
    print(f"路线总长  : {total / 1000:.2f} km")
    if elevations:
        print(f"海拔范围  : {min(elevations):.0f} ~ {max(elevations):.0f} m，总爬升约 {total_ascent(elevations):.0f} m")
    print()

    if not waypoints:
        print("命名航点  : 无 —— 卡片不会显示 CP 刻度，大数字退化为「距终点」")
        print("           导出 GPX 时请勾选「途经点 / 航点」。")
        return 0

    print(f"命名航点  : {len(waypoints)} 个")
    kept = 0
    for nm, lat, lon in waypoints:
        best_i, best_d = 0, float("inf")
        for i, p in enumerate(track):
            d = haversine((lat, lon), p[:2])
            if d < best_d:
                best_d, best_i = d, i
        route_dist = cum[best_i]
        by_name = any(w in nm.lower() for w in START_FINISH_WORDS)
        by_pos = route_dist <= START_END_MARGIN_M or route_dist >= total - START_END_MARGIN_M
        if by_name or by_pos:
            why = "名字是起终点" if by_name else "贴近起点/终点"
            print(f"  [排除] {nm or '(无名)':<18} 里程 {route_dist:7.0f} m  偏离轨迹 {best_d:5.0f} m  ← {why}")
        else:
            kept += 1
            print(f"  [CP  ] {nm or '(无名)':<18} 里程 {route_dist:7.0f} m  偏离轨迹 {best_d:5.0f} m")
    print()
    print(f"可用于显示的 CP 个数: {kept}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
