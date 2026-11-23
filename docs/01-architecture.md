# 架构说明

## 一句话

**手机算，卡片看。** GPS、GPX、路径计算全在手机上；卡片没有 GPS、不做路径计算，只解析一条 BLE 帧并渲染。

## 为什么这样分工

| 约束 | 结果 |
|---|---|
| 卡片是 ESP32-C3，8 MB Flash、**无 PSRAM** | 放不下离线地图/切片，也跑不动路径规划 |
| 卡片屏只有 240×320，挂在肩带上 | 显示必须"一眼可读"，信息量不能多 |
| 越野场景手上有杖、脚下不稳 | 交互只能是"低头看"和"按一下" |
| 手机本来就要带（拍照、求救、看轨迹） | 让手机承担计算是最自然的复用 |

于是分工是：

- **手机**：读 GPX（轨迹点、命名航点、海拔）→ 取 GPS → 算偏离/剩余/进度/到 CP 里程/累计 → 每 2 秒推一帧。
- **卡片**：解析帧 → 维护"最后一次有效数据" → 驱动 LVGL 界面 → 按键切页/调亮度。

卡片侧代码总共约 2000 行（不含字体），帧解析是纯 C 且可做主机测试。

## 固件结构

```
firmware/
├── main/
│   ├── main.c           入口：初始化 BSP → nav_ui_init → nav_ble_start
│   ├── nav_app.h/.c     全局状态容器（app_main 里是 static，避免栈上结构体悬垂）
│   ├── nav_ble.c        NimBLE：GATT 服务、广播、连接事件、接收写入
│   ├── nav_protocol.c   帧解析（纯逻辑，无 ESP-IDF 依赖，可主机测试）
│   ├── nav_state.c      状态锁 + 最后一次有效数据 + 更新入口
│   ├── nav_ui.c         LVGL 界面：导航页 / 数据页 / 进度条 / 按键
│   ├── nav_format.c     距离/海拔的人类可读格式化（纯逻辑）
│   └── nav_font.c       自绘 CJK 点阵字体（GB2312，7726 字，4bpp 未压缩）
└── components/bsp/      板级支持：ST7789P3 屏、ADC 三键、ES8311、CW2017
```

### 数据流

```
BLE 写入 (NAV 特征值)
      │  nav_ble: 累积到 nav_rx_t 缓冲区，遇 '\n' 成帧
      ▼
nav_protocol: 解析 14 个字段 → nav_update_t
      ▼
nav_state: 取锁 → 拷贝进 app->state.update
      ▼
nav_ui (500ms lv_timer): 取锁 → 拷贝快照 → 渲染
```

三个关键点：

1. **解析与渲染解耦**：BLE 回调只做解析和写状态，从不碰 LVGL。
2. **锁的边界**：`app->state_lock` 只保护状态结构体，渲染时先拷贝快照再释放锁，避免在持锁期间做耗时绘图。
3. **LVGL 只在 LVGL 任务里动**：BLE 回调和按键回调都不直接操作对象；按键回调通过 `bsp_lvgl_lock()` 拿锁后再改。

### 状态机

卡片侧维护三种状态，全部由帧里的 `state` 字段驱动：

| state | 含义 | 界面表现 |
|---|---|---|
| 0 | 待出发（未进起点 100m） | 橙色「距起点 XXX」/「等待 GPS 定位」 |
| 1 | 导航中 | 绿色「已出发」，显示完整导航数据 |
| 2 | 已到达终点 | 绿色「已到达终点」 |

额外维度是 `fix`（0/1）：`fix=0` 时状态颜色转橙，且**界面沿用最后一次有效数据**而不是清空——这是刻意的，越野中信号丢失比信息缺失更常见。

## App 结构

```
android/app/src/main/java/com/folotoy/navcompanion/
├── MainActivity.kt       单页界面：选 GPX、切方向、开始/停止
├── BleNavService.kt      前台服务（location + connectedDevice）：扫描、连接、定位、推帧
├── GpxParser.kt          GPX 解析：<trkpt>/<rtept>/<wpt>，手写扫描器
├── NavEngine.kt          双向导航核心：最近点、剩余、进度、CP 规划、下一 CP
├── TrackAccumulator.kt   累计距离/爬升（精度、速度、高度噪声三重过滤）
├── NavFrame.kt           帧构造（与固件 nav_protocol.h 严格对应）
└── GeoMath.kt            Haversine、点到线段距离、3D 距离、轨迹长度
```

### 为什么用前台服务

越野过程中锁屏是常态，Android 12+ 对后台定位与 BLE 有严格限制。服务以 `location | connectedDevice` 类型前台运行，配合常驻通知，才能在锁屏后持续定位并保持 BLE 连接。

### 为什么零第三方依赖

- 减小 APK 体积、减少供应链风险；
- BLE / 定位 / XML 解析都是系统 API，够用；
- 逻辑层（`NavEngine`、`TrackAccumulator`、`GpxParser`、`GeoMath`）全部是纯 Kotlin，可在 JVM 上直接单测。

### 方向与索引

轨迹点是一个数组，正穿时索引递增，反穿时索引递减。为了不让"递增/递减"扩散到每一处计算，`NavEngine` 内部统一换算成**逻辑索引**：

```
logical 0 ──────────────▶ logical n-1     （始终表示"行进方向上的起点 → 终点"）
实际索引 = 正穿 ? logical : n - 1 - logical
```

剩余里程、前方参考点、CP 排序、进度全部基于逻辑索引，方向只在一个函数里体现。

## 协议版本

帧格式是 **v0.3（14 字段）**，字段定义见 [02-ble-protocol.md](02-ble-protocol.md)。固件与 App 的帧格式必须一致，否则解析会失败——所以两者始终成对发布。

## 可测试性

| 层 | 测试方式 |
|---|---|
| 帧解析/格式化/状态机（C） | `firmware/tests/*.c`，主机编译运行 |
| 导航计算/累计/GPX 解析（Kotlin） | `android/app/src/test/`，`./gradlew testDebugUnitTest` |
| 设备行为 | 实机：刷机后看串口日志 + 屏幕 |

纯逻辑与 ESP-IDF/LVGL 解耦是刻意的设计约束：越野场景没法在桌面上复现，能自动化的部分越多越好。
