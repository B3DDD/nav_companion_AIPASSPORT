package com.folotoy.navcompanion

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * GPX 解析单测：用两种真实导出格式的结构做样本。
 *  - ovitalmap（GPX 1.0）：trkpt 在前，wpt 在文件末尾，name 在 time 之后
 *  - COROS（GPX 1.1）：metadata → wpt(含 extensions) → trk
 */
class GpxParserTest {

    private val ovitalSample = """
<?xml version="1.0" encoding="utf-8"?>
<gpx version="1.0" xmlns="http://www.topografix.com/GPX/1/0" creator="ovitalmap">
<time>2013-03-15T00:00:00Z</time>
<trk>
	<name>漓江跑23.6km</name>
	<trkseg>
		<trkpt lon="110.53449408" lat="24.92247219" >
			<ele>144</ele>
			<time>2026-11-12T00:17:47Z</time>
		</trkpt>
		<trkpt lon="110.53449970" lat="24.92246658" >
			<ele>145</ele>
			<time>2026-11-12T00:17:48Z</time>
		</trkpt>
		<trkpt lon="110.53451730" lat="24.92244956" >
			<ele>146</ele>
			<time>2026-11-12T00:17:49Z</time>
		</trkpt>
	</trkseg>
</trk>
<wpt lon="110.53438067" lat="24.92247103" >
	<time>2026-04-03T03:42:16Z</time>
	<name>起终点</name>
</wpt>
<wpt lon="110.50417364" lat="24.94947058" >
	<time>2026-04-03T03:10:47Z</time>
	<name>CP1 大龙崴</name>
</wpt>
</gpx>
""".trimIndent()

    private val corosSample = """
<?xml version="1.0" encoding="UTF-8"?>
<gpx xmlns="http://www.topografix.com/GPX/1/1" xmlns:gpxdata="http://www.cluetrust.com/XML/GPXDATA/1/0" creator="COROS Wearables" version="1.1">
 <metadata>
  <link href="https://www.coros.com"><text>COROS</text></link>
 </metadata>
 <wpt lat="27.0248133" lon="100.2216743">
  <ele>2780</ele>
  <name>起终点</name>
  <extensions>
   <coros_type>3</coros_type>
   <coros_flag>1</coros_flag>
  </extensions>
 </wpt>
 <wpt lat="27.0776356" lon="100.2299415">
  <ele>3277</ele>
  <name>cp1/7.5</name>
 </wpt>
 <trk>
  <trkseg>
   <trkpt lat="27.0248133" lon="100.2216743"><ele>2780</ele></trkpt>
   <trkpt lat="27.0250000" lon="100.2220000"><ele>2782</ele></trkpt>
  </trkseg>
 </trk>
</gpx>
""".trimIndent()

    @Test
    fun parsesOvitalFormat() {
        val d = GpxParser.parseText(ovitalSample)
        assertEquals(3, d.track.size)
        assertEquals(3, d.trkptCount)
        assertEquals(0, d.rteptCount)
        assertEquals(2, d.wptCount)
        assertEquals(2, d.waypoints.size)
        assertEquals("起终点", d.waypoints[0].name)
        assertEquals("CP1 大龙崴", d.waypoints[1].name)
        // 经纬度按属性名读取，与属性顺序无关
        assertEquals(24.92247219, d.track[0].lat, 1e-9)
        assertEquals(110.53449408, d.track[0].lon, 1e-9)
        assertEquals(144.0, d.track[0].ele!!, 1e-9)
    }

    @Test
    fun parsesCorosFormat() {
        val d = GpxParser.parseText(corosSample)
        assertEquals(2, d.track.size)
        assertEquals(2, d.trkptCount)
        assertEquals(2, d.wptCount)
        assertEquals(2, d.waypoints.size)
        assertEquals("起终点", d.waypoints[0].name)
        assertEquals("cp1/7.5", d.waypoints[1].name)
        assertEquals(2780.0, d.waypoints[0].ele!!, 1e-9)
        assertEquals(3277.0, d.waypoints[1].ele!!, 1e-9)
        // extensions 里的内容不应污染名字
        assertTrue(d.waypoints[0].name.length < 10)
    }

    @Test
    fun parsesRteptAsTrackAndNamedCp() {
        val sample = """
<?xml version="1.0" encoding="utf-8"?>
<gpx version="1.1" xmlns="http://www.topografix.com/GPX/1/1">
 <rte>
  <rtept lat="30.0" lon="120.0"><name>CP1</name></rtept>
  <rtept lat="30.001" lon="120.0"/>
  <rtept lat="30.002" lon="120.0"><name>CP2</name></rtept>
 </rte>
</gpx>
""".trimIndent()
        val d = GpxParser.parseText(sample)
        assertEquals(3, d.track.size)
        assertEquals(3, d.rteptCount)
        assertEquals(2, d.namedRteptCount)
        assertEquals(2, d.waypoints.size)
        assertEquals("CP1", d.waypoints[0].name)
        assertEquals("CP2", d.waypoints[1].name)
    }

    @Test
    fun ignoresTrackNameAsWaypoint() {
        val d = GpxParser.parseText(ovitalSample)
        // <trk><name> 是轨迹名，不能当成 CP
        assertTrue(d.waypoints.none { it.name.contains("漓江跑") })
    }
}
