package com.folotoy.navcompanion

/**
 * 导航帧构造：与卡片固件 nav_protocol.h 保持一致（v0.3）。
 * 帧格式: NAV|fix|state|wp|dist_m|off_m|remain_m|alt_m|cum_m|asc_m|total_m|prog_pm|cps|turn\n
 *   state: 0=待出发(remain 表示距起点距离), 1=导航中, 2=已完成
 *   cps  : 逗号分隔的 CP 千分比刻度，如 "120,350,610,900"
 */
object NavFrame {
    private fun safe(s: String): String = s.replace('|', ' ').replace('\n', ' ').replace('\r', ' ')

    /** 待出发：还没走到起点 100m 内，remain 字段承载"距起点距离"。 */
    fun buildPending(distToStartM: Int, totalM: Int, cpsStr: String): ByteArray =
        "NAV|0|0||-1|-1|$distToStartM|-1|-1|-1|$totalM|-1|${safe(cpsStr)}|\n"
            .toByteArray(Charsets.UTF_8)

    /** 导航中（有有效定位）。 */
    fun buildNav(
        r: NavEngine.NavResult,
        altM: Int,
        cumM: Int,
        ascM: Int,
        cpsStr: String,
        turn: String = ""
    ): ByteArray =
        "NAV|1|1|${safe(r.wpName)}|${r.distM}|${r.offM}|${r.remainM}|$altM|$cumM|$ascM|${r.totalM}|${r.progPm}|${safe(cpsStr)}|${safe(turn)}\n"
            .toByteArray(Charsets.UTF_8)

    /** 已到达终点。 */
    fun buildArrived(totalM: Int, cumM: Int, ascM: Int, altM: Int, cpsStr: String): ByteArray =
        "NAV|1|2||0|0|0|$altM|$cumM|$ascM|$totalM|1000|${safe(cpsStr)}|\n"
            .toByteArray(Charsets.UTF_8)

    /** 无有效定位：保留 state，卡片据此区分"待出发"与"导航中丢信号"。 */
    fun buildNoFix(state: Int): ByteArray =
        "NAV|0|$state||-1|-1|-1|-1|-1|-1|-1|-1||\n".toByteArray(Charsets.UTF_8)
}
