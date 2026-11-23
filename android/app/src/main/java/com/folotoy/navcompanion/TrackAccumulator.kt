package com.folotoy.navcompanion

/**
 * 累计器：把连续的 GPS 定位累加成"已走距离 / 累计爬升 / 已用时间"。
 *
 * 过滤规则（避免数据虚高）：
 *  - 精度 > 30m 的定位直接丢弃；
 *  - 相邻两次位移速度 > 10 m/s（36 km/h）视为 GPS 跳点，不计入距离；
 *  - 距离按 3D 叠加高度差，但垂直分量限制在 0.6×水平距离内（约 31° 坡）；
 *  - 爬升：高度先做 3 点滑动平均，再以 3m 滞回累计正增量。
 *
 * 纯逻辑，无 Android 依赖，可直接做 JVM 单测。
 */
class TrackAccumulator(
    private val maxAccuracyM: Float = 30f,
    private val maxSpeedMps: Double = 10.0,
    private val ascentHysteresisM: Double = 3.0
) {
    var distanceM: Double = 0.0
        private set
    var ascentM: Double = 0.0
        private set
    var elapsedMs: Long = 0L
        private set
    /** 被采纳的定位数（用于判断是否真的在移动）。 */
    var samples: Int = 0
        private set

    private var startMs: Long = 0L
    private var lastLat: Double? = null
    private var lastLon: Double? = null
    private var lastAltSmooth: Double? = null
    private var lastMs: Long = 0L
    private val altWindow = ArrayList<Double>(4)
    private var lowAlt: Double? = null

    /** 出发时清零并设定起点时间。 */
    fun reset(nowMs: Long) {
        distanceM = 0.0
        ascentM = 0.0
        elapsedMs = 0L
        samples = 0
        startMs = nowMs
        lastLat = null
        lastLon = null
        lastAltSmooth = null
        lastMs = nowMs
        altWindow.clear()
        lowAlt = null
    }

    /**
     * 喂入一次定位。返回 true 表示本次被采纳（参与累计）。
     * @param alt 海拔（米，可为 null）
     * @param accuracyM 水平精度（米）
     */
    fun add(lat: Double, lon: Double, alt: Double?, accuracyM: Float, tsMs: Long): Boolean {
        if (startMs == 0L) reset(tsMs)
        if (accuracyM > maxAccuracyM) return false

        elapsedMs = (tsMs - startMs).coerceAtLeast(0L)

        val pLat = lastLat
        val pLon = lastLon
        var accepted = true
        if (pLat != null && pLon != null) {
            val dt = (tsMs - lastMs) / 1000.0
            if (dt > 0.0) {
                val prev = GeoMath.LatLon(pLat, pLon)
                val cur = GeoMath.LatLon(lat, lon)
                val dh = GeoMath.distanceMeters(prev, cur)
                if (dh / dt > maxSpeedMps) {
                    // 跳点：不累加距离，但仍然推进参考点
                    accepted = false
                } else {
                    distanceM += GeoMath.distance3DMeters(prev, cur, lastAltSmooth, alt)
                    samples++
                }
            }
        }

        // 爬升：3 点滑动平均 + 3m 滞回
        if (alt != null) {
            altWindow.add(alt)
            if (altWindow.size > 3) altWindow.removeAt(0)
            val smooth = altWindow.average()
            lastAltSmooth = smooth
            val low = lowAlt
            if (low == null) {
                lowAlt = smooth
            } else if (smooth > low + ascentHysteresisM) {
                ascentM += smooth - low
                lowAlt = smooth
            } else if (smooth < low) {
                lowAlt = smooth
            }
        } else {
            lastAltSmooth = null
        }

        lastLat = lat
        lastLon = lon
        lastMs = tsMs
        return accepted
    }
}
