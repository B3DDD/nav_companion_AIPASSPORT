package com.folotoy.navcompanion

/**
 * 导航计算：由当前 GPS 位置与 GPX 轨迹推导
 * 剩余里程 / 偏离 / 进度 / 到下一 CP 的里程 / 下一 CP 名称。
 *
 * 支持正穿与反穿：内部统一换算成"逻辑索引"（0 = 行进方向上的起点）。
 * 纯逻辑，与 BLE / 定位解耦。
 */
object NavEngine {

    enum class Direction { FORWARD, REVERSE }

    /** 距路线起点/终点多少米以内的命名点不算 CP（起点、终点、起终点同点）。 */
    private const val START_END_MARGIN_M = 100.0

    /** 起点/终点的常见命名（中英双语，小写比较）。 */
    private val START_FINISH_WORDS = listOf(
        "起终点", "起点", "终点", "出发", "完赛",
        "start", "finish", "start/finish", "s/f"
    )

    /** 已规划好的 CP 点：名称 + 距路线起点的里程（米）。 */
    data class Checkpoint(val name: String, val routeDistM: Int)

    data class NavResult(
        val wpName: String,     // 下一 CP 名称（无 CP 时为空串）
        val distM: Int,         // 到下一 CP 的里程；无 CP 时为"前方参考点"距离
        val offM: Int,          // 偏离距离
        val remainM: Int,       // 剩余里程
        val totalM: Int,        // 路线总长
        val progPm: Int,        // 进度（千分比 0..1000）
        val nextCpDistM: Int    // 到下一 CP 的里程（-1 = 无 CP）
    )

    private fun actualIndex(n: Int, logical: Int, dir: Direction): Int =
        if (dir == Direction.FORWARD) logical else n - 1 - logical

    /** 行进方向上的起点（正穿 = 首点，反穿 = 末点）。 */
    fun startPoint(track: List<GeoMath.LatLon>, dir: Direction): GeoMath.LatLon? =
        when {
            track.isEmpty() -> null
            dir == Direction.FORWARD -> track.first()
            else -> track.last()
        }

    /**
     * 把命名航点（GPX 的 wpt/rtept）映射成"距路线起点的里程"，并按行进方向排序。
     *
     * 会剔除起点 / 终点（以及"起终点"同点），只保留真正的 CP（补给站、检查点等）：
     *  1) 名字含 起终点/起点/终点/start/finish 之类的关键词；
     *  2) 或者沿路线里程落在起点或终点 ±100m 内（覆盖名字没写"起点"的情况）。
     */
    fun planCheckpoints(
        track: List<GeoMath.LatLon>,
        waypoints: List<GpxParser.Waypoint>,
        dir: Direction
    ): List<Checkpoint> {
        if (track.size < 2 || waypoints.isEmpty()) return emptyList()
        val n = track.size
        val total = GeoMath.trackLength(track, 0, n - 1)

        val out = ArrayList<Checkpoint>(waypoints.size)
        for (w in waypoints) {
            if (isStartFinishName(w.name)) continue

            // 找最近的轨迹点
            var bestIdx = 0
            var bestD = Double.MAX_VALUE
            for (i in 0 until n) {
                val d = GeoMath.distanceMeters(GeoMath.LatLon(w.lat, w.lon), track[i])
                if (d < bestD) {
                    bestD = d
                    bestIdx = i
                }
            }
            val routeDist = if (dir == Direction.FORWARD) {
                GeoMath.trackLength(track, 0, bestIdx)
            } else {
                GeoMath.trackLength(track, bestIdx, n - 1)
            }

            // 位置过滤：贴起点或贴终点的点不算 CP
            if (routeDist <= START_END_MARGIN_M) continue
            if (routeDist >= total - START_END_MARGIN_M) continue

            out.add(Checkpoint(cleanCpName(w.name), GeoMath.metersToInt(routeDist)))
        }
        out.sortBy { it.routeDistM }
        return out
    }

    /** 起点/终点/起终点之类的命名（中英双语）。 */
    private fun isStartFinishName(name: String): Boolean {
        val s = name.trim().lowercase()
        if (s.isEmpty()) return false
        for (kw in START_FINISH_WORDS) {
            if (s.contains(kw)) return true
        }
        return false
    }

    /**
     * 清理 CP 名称用于显示：
     *  "cp1/7.5" -> "CP1"（去掉 GPX 里附带的公里数），其余原样保留。
     */
    private fun cleanCpName(raw: String): String {
        val s = raw.trim()
        if (s.isEmpty()) return s
        val m = Regex("^cp\\s*(\\d+)\\s*/.*$", RegexOption.IGNORE_CASE).find(s)
        if (m != null) return "CP${m.groupValues[1]}"
        return s
    }

    fun compute(
        track: List<GeoMath.LatLon>,
        checkpoints: List<Checkpoint>,
        pos: GeoMath.LatLon,
        direction: Direction
    ): NavResult? {
        if (track.size < 2) return null
        val n = track.size

        val (segIdx, offDist, t) = GeoMath.nearestOnTrackDetail(pos, track)
        if (segIdx < 0) return null
        val nearestPoint = if (t < 0.5) segIdx else minOf(segIdx + 1, n - 1)
        val logical = if (direction == Direction.FORWARD) nearestPoint else n - 1 - nearestPoint

        // 剩余里程
        var remain = 0.0
        var i = logical
        while (i < n - 1) {
            remain += GeoMath.distanceMeters(
                track[actualIndex(n, i, direction)],
                track[actualIndex(n, i + 1, direction)]
            )
            i++
        }
        val totalM = GeoMath.trackLength(track, 0, n - 1)
        val covered = (totalM - remain).coerceAtLeast(0.0)
        val progPm = if (totalM > 0.0) ((covered / totalM) * 1000.0).toInt().coerceIn(0, 1000) else 0

        // 下一 CP：第一个"已跑里程之后"的 CP（10m 滞回，避免抖动跳名）
        var nextCpName = ""
        var nextCpDist = -1
        for (cp in checkpoints) {
            if (cp.routeDistM > covered + 10.0) {
                nextCpName = cp.name
                nextCpDist = (cp.routeDistM - covered).toInt().coerceAtLeast(0)
                break
            }
        }

        // 没有 CP 了（含"最后一个 CP 之后"和"整条路线没有 CP"）：改为显示离终点的里程
        if (nextCpDist < 0) {
            nextCpName = "终点"
            nextCpDist = GeoMath.metersToInt(remain)
        }

        return NavResult(
            wpName = nextCpName,
            distM = nextCpDist,
            offM = GeoMath.metersToInt(offDist),
            remainM = GeoMath.metersToInt(remain),
            totalM = GeoMath.metersToInt(totalM),
            progPm = progPm,
            nextCpDistM = nextCpDist
        )
    }

    /** 路线总爬升：用 GPX 的 <ele> 预计算（3m 滞回，过滤高度抖动）。 */
    fun totalAscent(track: List<GeoMath.LatLon>, hysteresisM: Double = 3.0): Double {
        var ascent = 0.0
        var lowest = Double.MAX_VALUE
        for (p in track) {
            val e = p.ele ?: continue
            if (lowest == Double.MAX_VALUE) {
                lowest = e
                continue
            }
            if (e > lowest + hysteresisM) {
                ascent += e - lowest
                lowest = e
            } else if (e < lowest) {
                lowest = e
            }
        }
        return ascent
    }
}
