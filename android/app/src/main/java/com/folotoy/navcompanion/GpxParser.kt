package com.folotoy.navcompanion

import java.io.InputStream

/**
 * GPX 解析：提取轨迹点（含海拔）与命名航点（CP 点）。
 *
 * 支持：
 *  - `<trkpt>`  记录轨迹（两步路/高驰/ovitalmap 导出）
 *  - `<rtept>`  规划路线；有名字的同时视为 CP 点
 *  - `<wpt>`    独立命名航点（最常见的 CP 表示）
 *
 * 为什么不用 XmlPullParser：不同实现的 `getName()` 对默认命名空间
 * （`xmlns="http://www.topografix.com/GPX/1/1"`）处理不一致，会导致标签匹配失败。
 * 这里用纯字符扫描，行为可预测、可在 JVM 单测里直接验证。
 */
object GpxParser {
    data class Waypoint(val name: String, val lat: Double, val lon: Double, val ele: Double? = null)

    data class GpxData(
        val track: List<GeoMath.LatLon>,
        val waypoints: List<Waypoint>,
        val trkptCount: Int = 0,
        val rteptCount: Int = 0,
        val wptCount: Int = 0,
        val namedRteptCount: Int = 0
    )

    private enum class Kind { NONE, TRKPT, RTEPT, WPT }

    fun parse(input: InputStream): GpxData =
        parseText(input.readBytes().decodeToString())

    /** 从属性串里取 `name="value"`。 */
    private fun attr(inner: String, name: String): String? {
        val key = "$name="
        var idx = inner.indexOf(key)
        while (idx >= 0) {
            val before = if (idx == 0) ' ' else inner[idx - 1]
            if (before.isWhitespace()) {
                var p = idx + key.length
                if (p < inner.length && (inner[p] == '"' || inner[p] == '\'')) {
                    val quote = inner[p]
                    val end = inner.indexOf(quote, p + 1)
                    if (end > p) return inner.substring(p + 1, end)
                }
                return null
            }
            idx = inner.indexOf(key, idx + 1)
        }
        return null
    }

    /** 纯字符串版本，便于单元测试。 */
    fun parseText(text: String): GpxData {
        val track = ArrayList<GeoMath.LatLon>()
        val waypoints = ArrayList<Waypoint>()
        var trkptCount = 0
        var rteptCount = 0
        var wptCount = 0
        var namedRteptCount = 0

        var kind = Kind.NONE
        var lat = 0.0
        var lon = 0.0
        var ele: Double? = null
        var inName = false
        var inEle = false
        val nameBuf = StringBuilder()
        val eleBuf = StringBuilder()

        val n = text.length
        var i = 0
        while (i < n) {
            val lt = text.indexOf('<', i)
            if (lt < 0) break

            // 跳过注释 / 处理指令 / DOCTYPE
            if (text.startsWith("<!--", lt)) {
                val e = text.indexOf("-->", lt)
                i = if (e < 0) n else e + 3
                continue
            }
            if (text.startsWith("<?", lt)) {
                val e = text.indexOf("?>", lt)
                i = if (e < 0) n else e + 2
                continue
            }
            if (text.startsWith("<!", lt)) {
                val e = text.indexOf('>', lt)
                i = if (e < 0) n else e + 1
                continue
            }

            val gt = text.indexOf('>', lt)
            if (gt < 0) break

            var inner = text.substring(lt + 1, gt)
            var closing = false
            if (inner.startsWith("/")) {
                closing = true
                inner = inner.substring(1)
            }
            var selfClosing = false
            val trimmed = inner.trimEnd()
            if (trimmed.endsWith("/")) {
                selfClosing = true
                inner = trimmed.dropLast(1)
            }

            val sp = inner.indexOfFirst { it.isWhitespace() }
            val rawTag = (if (sp >= 0) inner.substring(0, sp) else inner).trim()
            val tag = if (rawTag.contains(':')) rawTag.substringAfter(':') else rawTag

            if (!closing) {
                when (tag) {
                    "trkpt", "rtept", "wpt" -> {
                        kind = when (tag) {
                            "trkpt" -> Kind.TRKPT
                            "rtept" -> Kind.RTEPT
                            else -> Kind.WPT
                        }
                        lat = attr(inner, "lat")?.toDoubleOrNull() ?: 0.0
                        lon = attr(inner, "lon")?.toDoubleOrNull() ?: 0.0
                        ele = null
                        nameBuf.setLength(0)
                        if (selfClosing) {
                            // <wpt lat=".." lon=".." />  —— 无子元素
                            if (kind == Kind.WPT) {
                                wptCount++
                                waypoints.add(Waypoint("", lat, lon, null))
                            } else {
                                track.add(GeoMath.LatLon(lat, lon, null))
                                if (kind == Kind.TRKPT) trkptCount++ else rteptCount++
                            }
                            kind = Kind.NONE
                        }
                    }
                    "name" -> if (kind != Kind.NONE) {
                        inName = true
                        nameBuf.setLength(0)
                    }
                    "ele" -> if (kind != Kind.NONE) {
                        inEle = true
                        eleBuf.setLength(0)
                    }
                }
            } else {
                when (tag) {
                    "name" -> inName = false
                    "ele" -> {
                        ele = eleBuf.toString().trim().toDoubleOrNull()
                        inEle = false
                    }
                    "trkpt" -> {
                        track.add(GeoMath.LatLon(lat, lon, ele))
                        trkptCount++
                        kind = Kind.NONE
                    }
                    "rtept" -> {
                        track.add(GeoMath.LatLon(lat, lon, ele))
                        rteptCount++
                        val nm = nameBuf.toString().trim()
                        if (nm.isNotEmpty()) {
                            waypoints.add(Waypoint(nm, lat, lon, ele))
                            namedRteptCount++
                        }
                        kind = Kind.NONE
                    }
                    "wpt" -> {
                        wptCount++
                        waypoints.add(Waypoint(nameBuf.toString().trim(), lat, lon, ele))
                        kind = Kind.NONE
                    }
                }
            }

            // 捕获标签之后的文本（只有 name/ele 需要）
            val nextLt = text.indexOf('<', gt + 1)
            val textEnd = if (nextLt < 0) n else nextLt
            if (inName) nameBuf.append(text, gt + 1, textEnd)
            if (inEle) eleBuf.append(text, gt + 1, textEnd)

            i = gt + 1
        }
        return GpxData(track, waypoints, trkptCount, rteptCount, wptCount, namedRteptCount)
    }
}
