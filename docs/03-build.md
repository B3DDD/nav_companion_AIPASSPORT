# 构建说明

两部分互相独立：固件用 ESP-IDF，App 用 Gradle。**协议版本必须匹配**，所以改动帧格式时要一起重编。

## 环境要求

| 组件 | 版本 | 说明 |
|---|---|---|
| ESP-IDF | **5.5.3** | 与官方 AI Passport 模板一致，其他版本未验证 |
| Python | 3.8+ | ESP-IDF 自带虚拟环境即可 |
| JDK | **17** | AGP 8.x 要求 |
| Android SDK | platform **34** + build-tools **34.0.0** | `compileSdk 34` |
| Gradle | 8.9 | 仓库已带 wrapper |

---

## 一、固件

### 1. 准备 ESP-IDF

```bash
git clone -b v5.5.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32c3
. ./export.sh
```

Windows 用 `install.bat esp32c3` 与 `export.bat`。

> 国内网络下载组件慢时，可设置镜像：
> `export IDF_COMPONENT_STORAGE_URL=https://components-file.espressif.cn`

### 2. 编译

```bash
cd firmware
idf.py set-target esp32c3
idf.py build
```

产物在 `build/FoloToy-AI-Passport.bin`。目标是 ESP32-C3、8 MB Flash、**无 PSRAM**。

### 3. 生成合并镜像

卡片需要从 `0x0` 刷入的合并镜像（bootloader + 分区表 + 应用）：

```bash
idf.py merge-bin -o FoloToy-AI-Passport-full.bin
```

### 4. 刷入

```bash
python -m esptool --chip esp32c3 -p /dev/ttyUSB0 -b 460800 \
  write_flash 0x0 FoloToy-AI-Passport-full.bin
```

Windows 端口形如 `COM3`。刷完自动复位，屏幕出现「等待手机连接」即正常。

### 5. 主机侧单元测试

`firmware/tests/` 下的测试不依赖 ESP-IDF 与 LVGL，用本机 C 编译器即可：

```bash
cc -std=c11 -Wall -Wextra -Werror -Ifirmware/main \
   firmware/tests/test_nav_protocol.c firmware/main/nav_protocol.c \
   -o /tmp/test_nav_protocol && /tmp/test_nav_protocol
```

同理可跑 `test_nav_format.c`、`test_nav_state.c`（后者需要对应的 `.c`）。

### 6. 重新生成中文字体（一般不需要）

`firmware/main/nav_font.c` 是用 `lv_font_conv` 从黑体生成的点阵字体（GB2312，7726 字，4bpp，**未压缩**）。只有需要增删字形时才重新生成：

```bash
npx lv_font_conv --font SimHei.ttf --size 16 --bpp 4 --format lvgl \
  --range 0x20-0x7F --symbols <GB2312 字符表> --no-compress \
  -o nav_font.c --lv-include lvgl.h
```

要点：

- **必须加 `--no-compress`**：压缩格式需要解码器支持，未启用时字会显示成方块；
- 字体名（`nav_cjk`）需与 `nav_ui.c` 中的引用一致；
- 数字/拉丁仍回退到 `lv_font_montserrat_16`。

---

## 二、Android App

### 1. 编译

```bash
cd android
./gradlew assembleDebug
```

产物：`app/build/outputs/apk/debug/app-debug.apk`。

### 2. 单元测试

```bash
./gradlew testDebugUnitTest
```

纯 JVM 测试，覆盖：GPX 解析（两种真实导出格式）、双向导航对称性、CP 规划与过滤、进度计算、累计距离/爬升的噪声过滤。当前 **22 个用例**。

### 3. 依赖源

`settings.gradle.kts` 里优先使用阿里云镜像，官方仓库作为兜底：

```kotlin
repositories {
    maven { url = uri("https://maven.aliyun.com/repository/google") }
    maven { url = uri("https://maven.aliyun.com/repository/public") }
    google()
    mavenCentral()
}
```

---

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| `ModuleNotFoundError: No module named 'ldgen.entity'` | ESP-IDF 解压不完整，缺 `tools/ldgen/ldgen/__init__.py`；从官方压缩包补回该文件 |
| 中文显示为方块 | 字体用了压缩格式，或字库缺字：确认生成时带 `--no-compress`，并检查字形范围 |
| 编译报 `format-truncation` | 某个 `char buf[]` 太小，按报错加大缓冲区（本项目已把电量缓冲改为 16 字节） |
| Gradle 报 `debug.keystore.lock` 拒绝访问 | 把 `ANDROID_USER_HOME` 指到可写目录 |
| Kotlin 编译报临时文件拒绝访问 | 设置 `TEMP`/`TMP` 与 `-Djava.io.tmpdir` 到可写目录 |
| 卡片连上随即断开（`reason=531`） | 自定义 GATT 服务未注册成功：`ble_gatts_add_svcs` 必须在主机任务启动前调用 |
| App 装到手机闪退 | 检查是否给了定位、附近设备、通知三项权限；Android 12 以下不支持（`minSdk 31`） |
