# Android App

单 Activity + 一个前台服务的极简结构，**零第三方运行时依赖**（除 Kotlin 标准库）。

## 目录

```
android/app/src/main/java/com/folotoy/navcompanion/
├── MainActivity.kt       选 GPX、切正穿/反穿、开始/停止；轮询服务状态显示
├── BleNavService.kt      前台服务：扫描 → 连接 → 定位 → 推帧（含心跳与重连状态）
├── GpxParser.kt          GPX 解析：<trkpt> / <rtept> / <wpt>，手写扫描器
├── NavEngine.kt          双向导航：最近点、剩余、进度、CP 规划与过滤
├── TrackAccumulator.kt   累计距离/爬升，带精度、速度、高度噪声过滤
├── NavFrame.kt           BLE 帧构造（对应固件 nav_protocol.h）
└── GeoMath.kt            Haversine、点到线段距离、3D 距离、轨迹长度
```

`app/src/test/` 下是对应的 JVM 单测（GPX 解析、导航逻辑、累计器）。

## 构建

```bash
./gradlew assembleDebug      # app/build/outputs/apk/debug/app-debug.apk
./gradlew testDebugUnitTest  # 22 个 JVM 用例
```

需要 JDK 17 与 Android SDK 34。依赖解析优先走阿里云镜像，官方仓库兜底。

## 关键实现点

- **前台服务类型**：`location | connectedDevice`，否则锁屏后定位与 BLE 会被系统限制。
- **定位参数**：`GPS_PROVIDER`，最小间隔 2 秒、最小位移 2 米。
- **扫描策略**：`SCAN_MODE_LOW_LATENCY`，先按 Service UUID 匹配，设备名作兜底。
- **心跳**：3 秒没有新帧就补发一条「无定位」帧，让卡片能区分"丢信号"和"手机没连上"。
- **方向**：`EXTRA_REVERSE` 通过 Intent 传入服务；`NavEngine` 内部统一成逻辑索引。
- **起点锚定**：连续 2 次进入起点 100m 才置位 `armed`，此后才开始累计。

## 已知限制

- `minSdk 31`（Android 12）：使用了较新的前台服务与蓝牙权限模型。
- APK 为 debug 签名，仅用于侧载。
- 未使用任何厂商私有 API；不同品牌的差异主要在后台保活策略（见 [../docs/04-usage.md](../docs/04-usage.md)）。
