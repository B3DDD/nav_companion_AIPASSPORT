package com.folotoy.navcompanion

import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * 地理计算: 球面距离、点到线段的近似最短距离、轨迹最近点索引。
 * 全部为纯函数, 便于单元测试与复算。
 */
object GeoMath {
    const val EARTH_RADIUS_M = 6371000.0

    data class LatLon(val lat: Double, val lon: Double, val ele: Double? = null)

    /** 两点球面距离(米), Haversine。 */
    fun distanceMeters(a: LatLon, b: LatLon): Double {
        val dLat = Math.toRadians(b.lat - a.lat)
        val dLon = Math.toRadians(b.lon - a.lon)
        val la1 = Math.toRadians(a.lat)
        val la2 = Math.toRadians(b.lat)
        val h = sin(dLat / 2) * sin(dLat / 2) +
                cos(la1) * cos(la2) * sin(dLon / 2) * sin(dLon / 2)
        return 2 * EARTH_RADIUS_M * asin(sqrt(h))
    }

    /**
     * 3D 直线距离: 水平距离叠加垂直高差。
     * 垂直分量限制在 |Δalt| <= 0.6 × 水平距离（约 31° 坡），避免 GPS 高度噪声虚增里程。
     */
    fun distance3DMeters(a: LatLon, b: LatLon, altA: Double?, altB: Double?): Double {
        val dh = distanceMeters(a, b)
        if (altA == null || altB == null) return dh
        val raw = kotlin.math.abs(altB - altA)
        val dv = kotlin.math.min(raw, dh * 0.6)
        return sqrt(dh * dh + dv * dv)
    }

    /**
     * 点到线段 [a,b] 的最短距离(米)与垂足比例 t(0..1)。
     * 用等距圆柱近似(轨迹局部范围精度足够)。
     */
    fun distanceToSegmentMeters(p: LatLon, a: LatLon, b: LatLon): Pair<Double, Double> {
        val scale = cos(Math.toRadians((a.lat + b.lat) / 2))
        val dx = (b.lon - a.lon) * scale
        val dy = b.lat - a.lat
        val px = (p.lon - a.lon) * scale
        val py = p.lat - a.lat
        val len2 = dx * dx + dy * dy
        val t = if (len2 > 0.0) ((px * dx + py * dy) / len2).coerceIn(0.0, 1.0) else 0.0
        val cx = a.lon + (b.lon - a.lon) * t
        val cy = a.lat + (b.lat - a.lat) * t
        return Pair(distanceMeters(p, LatLon(cy, cx)), t)
    }

    /** 返回轨迹上距 p 最近的段索引, 以及最短距离(米)。 */
    fun nearestOnTrack(p: LatLon, track: List<LatLon>): Pair<Int, Double> {
        if (track.isEmpty()) return Pair(-1, Double.POSITIVE_INFINITY)
        if (track.size == 1) return Pair(0, distanceMeters(p, track[0]))
        var bestIdx = 0
        var bestDist = Double.MAX_VALUE
        for (i in 0 until track.size - 1) {
            val (d, _) = distanceToSegmentMeters(p, track[i], track[i + 1])
            if (d < bestDist) {
                bestDist = d
                bestIdx = i
            }
        }
        return Pair(bestIdx, bestDist)
    }

    /**
     * 最近段索引 + 垂足比例 t(0..1) + 最短距离(米)。
     * t 用来判断更靠近段的哪一端，从而得到"最近点"的轨迹点索引。
     */
    fun nearestOnTrackDetail(p: LatLon, track: List<LatLon>): Triple<Int, Double, Double> {
        if (track.isEmpty()) return Triple(-1, Double.POSITIVE_INFINITY, 0.0)
        if (track.size == 1) return Triple(0, distanceMeters(p, track[0]), 0.0)
        var bestIdx = 0
        var bestDist = Double.MAX_VALUE
        var bestT = 0.0
        for (i in 0 until track.size - 1) {
            val (d, t) = distanceToSegmentMeters(p, track[i], track[i + 1])
            if (d < bestDist) {
                bestDist = d
                bestIdx = i
                bestT = t
            }
        }
        return Triple(bestIdx, bestDist, bestT)
    }

    /** 沿轨迹从 fromIdx 累计到 toIdx(含两端点)的里程(米)。 */
    fun trackLength(track: List<LatLon>, fromIdx: Int, toIdx: Int): Double {
        if (fromIdx < 0 || toIdx >= track.size || fromIdx > toIdx) return 0.0
        var total = 0.0
        for (i in fromIdx until toIdx) {
            total += distanceMeters(track[i], track[i + 1])
        }
        return total
    }

    /** 距离四舍五入到整数米(负数视为不可用)。 */
    fun metersToInt(meters: Double): Int = meters.roundToInt()
}
