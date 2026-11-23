package com.folotoy.navcompanion

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 纯逻辑单测：双向导航、起点锚定、累计距离/爬升。
 * 轨迹沿纬线直线，每 0.001° 纬度约 111.2m。
 */
class NavLogicTest {

    private fun straightTrack(n: Int = 5): List<GeoMath.LatLon> =
        (0 until n).map { GeoMath.LatLon(30.0 + it * 0.001, 120.0) }

    @Test
    fun forwardRemainFullAtStartAndZeroAtEnd() {
        val track = straightTrack()
        val total = GeoMath.trackLength(track, 0, track.size - 1)

        val atStart = NavEngine.compute(track, emptyList(), track.first(), NavEngine.Direction.FORWARD)
        assertNotNull(atStart)
        assertEquals(total, atStart!!.remainM.toDouble(), 5.0)

        val atEnd = NavEngine.compute(track, emptyList(), track.last(), NavEngine.Direction.FORWARD)
        assertEquals(0.0, atEnd!!.remainM.toDouble(), 5.0)
    }

    @Test
    fun reverseRemainIsMirrored() {
        val track = straightTrack()
        val total = GeoMath.trackLength(track, 0, track.size - 1)

        // 反穿：站在原轨迹末点 = 反穿起点，剩余应为全长
        val atReverseStart = NavEngine.compute(track, emptyList(), track.last(), NavEngine.Direction.REVERSE)
        assertNotNull(atReverseStart)
        assertEquals(total, atReverseStart!!.remainM.toDouble(), 5.0)

        // 反穿：站在原轨迹首点 = 反穿终点，剩余应为 0
        val atReverseEnd = NavEngine.compute(track, emptyList(), track.first(), NavEngine.Direction.REVERSE)
        assertEquals(0.0, atReverseEnd!!.remainM.toDouble(), 5.0)
    }

    @Test
    fun startPointDependsOnDirection() {
        val track = straightTrack()
        assertEquals(track.first(), NavEngine.startPoint(track, NavEngine.Direction.FORWARD))
        assertEquals(track.last(), NavEngine.startPoint(track, NavEngine.Direction.REVERSE))
    }

    @Test
    fun offDistanceIsPerpendicular() {
        val track = straightTrack()
        // 偏离轨迹约 0.00045° 纬度 ≈ 50m
        val offPos = GeoMath.LatLon(30.002, 120.00045)
        val r = NavEngine.compute(track, emptyList(), offPos, NavEngine.Direction.FORWARD)
        assertNotNull(r)
        assertTrue("off=${r!!.offM}", r.offM in 40..60)
    }

    @Test
    fun startAndFinishAreNotCheckpoints() {
        val track = straightTrack(5)   // 全长约 445m
        val wps = listOf(
            GpxParser.Waypoint("起终点", track[0].lat, track[0].lon),
            GpxParser.Waypoint("CP1", track[2].lat, track[2].lon),   // 约 222m
            GpxParser.Waypoint("终点", track[4].lat, track[4].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)
        assertEquals(1, cps.size)
        assertEquals("CP1", cps[0].name)
    }

    @Test
    fun unnamedStartFinishExcludedByPosition() {
        val track = straightTrack(5)
        val wps = listOf(
            GpxParser.Waypoint("停车场", track[0].lat, track[0].lon),  // 贴起点，名字无关键词
            GpxParser.Waypoint("CP1", track[2].lat, track[2].lon),
            GpxParser.Waypoint("大门口", track[4].lat, track[4].lon)   // 贴终点
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)
        assertEquals(1, cps.size)
        assertEquals("CP1", cps[0].name)
    }

    @Test
    fun reverseDirectionStillExcludesStartFinish() {
        val track = straightTrack(5)
        val wps = listOf(
            GpxParser.Waypoint("起终点", track[0].lat, track[0].lon),
            GpxParser.Waypoint("CP1", track[2].lat, track[2].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.REVERSE)
        assertEquals(1, cps.size)
        assertEquals("CP1", cps[0].name)
    }

    @Test
    fun checkpointsAreMappedToRouteDistance() {
        val track = straightTrack(5)   // 4 段，每段约 111m
        val wps = listOf(
            GpxParser.Waypoint("CP1", track[1].lat, track[1].lon),
            GpxParser.Waypoint("CP2", track[3].lat, track[3].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)
        assertEquals(2, cps.size)
        assertEquals("CP1", cps[0].name)
        assertEquals("CP2", cps[1].name)
        assertTrue("cp1=${cps[0].routeDistM}", cps[0].routeDistM in 100..125)
        assertTrue("cp2=${cps[1].routeDistM}", cps[1].routeDistM in 320..350)
    }

    @Test
    fun reverseCheckpointsAreMirrored() {
        val track = straightTrack(5)
        val wps = listOf(
            GpxParser.Waypoint("CP1", track[1].lat, track[1].lon),
            GpxParser.Waypoint("CP2", track[3].lat, track[3].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.REVERSE)
        // 反穿时 CP2 变成"第一个到达的"
        assertEquals("CP2", cps[0].name)
        assertTrue("cp2rev=${cps[0].routeDistM}", cps[0].routeDistM in 100..125)
    }

    @Test
    fun progressAndNextCp() {
        val track = straightTrack(5)
        val wps = listOf(
            GpxParser.Waypoint("CP1", track[1].lat, track[1].lon),
            GpxParser.Waypoint("CP2", track[3].lat, track[3].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)

        // 起点：进度 0，下一 CP 是 CP1
        val atStart = NavEngine.compute(track, cps, track.first(), NavEngine.Direction.FORWARD)
        assertNotNull(atStart)
        val startRes = atStart!!
        assertEquals(0.0, startRes.progPm.toDouble(), 30.0)
        assertEquals("CP1", startRes.wpName)
        assertTrue("dist=${startRes.distM}", startRes.distM in 100..125)

        // 中点（track[2]）：进度约 500
        val atMid = NavEngine.compute(track, cps, track[2], NavEngine.Direction.FORWARD)
        assertNotNull(atMid)
        assertEquals(500.0, atMid!!.progPm.toDouble(), 60.0)
    }

    @Test
    fun noCheckpointsShowsFinishDistance() {
        val track = straightTrack(5)
        val r = NavEngine.compute(track, emptyList(), track[2], NavEngine.Direction.FORWARD)
        assertNotNull(r)
        val res = r!!
        // 没有 CP 时，大数字改为"离终点里程"
        assertEquals("终点", res.wpName)
        assertEquals(res.remainM, res.distM)
        assertTrue("remain=${res.remainM}", res.remainM in 200..250)
    }

    @Test
    fun afterLastCpShowsFinishDistance() {
        val track = straightTrack(5)
        val wps = listOf(GpxParser.Waypoint("CP1", track[1].lat, track[1].lon))
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)
        // 跑到 CP1 之后再往前，应显示"终点"而不是残留 CP1
        val r = NavEngine.compute(track, cps, track[3], NavEngine.Direction.FORWARD)
        assertNotNull(r)
        assertEquals("终点", r!!.wpName)
        assertEquals(r.remainM, r.distM)
    }

    @Test
    fun cpNameIsCleanedForDisplay() {
        val track = straightTrack(5)
        val wps = listOf(
            GpxParser.Waypoint("cp1/7.5", track[1].lat, track[1].lon),
            GpxParser.Waypoint("CP2 大龙崴", track[3].lat, track[3].lon)
        )
        val cps = NavEngine.planCheckpoints(track, wps, NavEngine.Direction.FORWARD)
        assertEquals("CP1", cps[0].name)          // "cp1/7.5" -> "CP1"
        assertEquals("CP2 大龙崴", cps[1].name)   // 其他名字原样保留
    }

    @Test
    fun totalAscentIgnoresSmallNoise() {
        val track = listOf(
            GeoMath.LatLon(30.0, 120.0, 1000.0),
            GeoMath.LatLon(30.001, 120.0, 1000.8),   // 抖动
            GeoMath.LatLon(30.002, 120.0, 1001.2),   // 抖动
            GeoMath.LatLon(30.003, 120.0, 1010.0),   // 真爬升
            GeoMath.LatLon(30.004, 120.0, 1005.0),   // 下降不计
            GeoMath.LatLon(30.005, 120.0, 1012.0)    // 再爬升
        )
        val ascent = NavEngine.totalAscent(track)
        // 1000→1010 约 10m，1005→1012 约 7m，合计约 17m
        assertTrue("ascent=$ascent", ascent in 14.0..20.0)
    }

    @Test
    fun accumulatorAddsDistance() {
        val acc = TrackAccumulator()
        acc.reset(0L)
        var lat = 30.0
        var t = 0L
        repeat(10) {
            acc.add(lat, 120.0, null, 5f, t)
            lat += 0.00005          // ≈ 5.56m
            t += 5000L
        }
        // 9 段 × 约 5.56m ≈ 50m
        assertTrue("dist=${acc.distanceM}", acc.distanceM in 40.0..60.0)
    }

    @Test
    fun accumulatorRejectsGpsJump() {
        val acc = TrackAccumulator()
        acc.reset(0L)
        acc.add(30.0, 120.0, null, 5f, 0L)
        acc.add(30.1, 120.0, null, 5f, 5000L)   // 5 秒跳 ~11km
        assertEquals(0.0, acc.distanceM, 1.0)
    }

    @Test
    fun accumulatorRejectsPoorAccuracy() {
        val acc = TrackAccumulator()
        acc.reset(0L)
        acc.add(30.0, 120.0, null, 5f, 0L)
        val accepted = acc.add(30.00005, 120.0, null, 200f, 5000L)  // 精度 200m 丢弃
        assertTrue(!accepted)
        assertEquals(0.0, acc.distanceM, 0.001)
    }

    @Test
    fun accumulatorAscentWithHysteresis() {
        val acc = TrackAccumulator()
        acc.reset(0L)
        val alts = listOf(1000.0, 1001.0, 1000.5, 1002.0, 1006.0, 1007.0, 1003.0)
        var t = 0L
        for (e in alts) {
            acc.add(30.0, 120.0, e, 5f, t)
            t += 5000L
        }
        // 3 点平滑 + 3m 滞回：只应记到 1000→1005 这一段
        assertTrue("asc=${acc.ascentM}", acc.ascentM in 3.0..7.0)
    }
}
