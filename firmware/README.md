# 固件（ESP-IDF）

AI Passport 模板的 fork，把模板的演示菜单替换成了导航副屏界面。

## 目录

```
firmware/
├── main/
│   ├── main.c          入口：BSP 初始化 → nav_ui_init → nav_ble_start
│   ├── nav_app.c/.h    全局状态容器（static，避免栈上结构体悬垂）
│   ├── nav_ble.c/.h    NimBLE GATT 服务、广播、连接事件、接收写入
│   ├── nav_protocol.*  帧解析（纯逻辑，无 ESP-IDF 依赖）
│   ├── nav_state.*     状态锁 + 最后一次有效数据
│   ├── nav_ui.c/.h     LVGL 界面：导航页 / 数据页 / 进度条 / 按键
│   ├── nav_format.*    距离、海拔格式化（纯逻辑）
│   ├── nav_font.c      自绘 GB2312 点阵字体（7726 字，4bpp 未压缩，约 6 MB）
│   └── demo_*.c        模板自带的演示页（保留，未在导航界面中使用）
├── components/bsp/     板级支持：ST7789P3、ADC 三键、ES8311、CW2017
├── tests/              主机侧单元测试（协议 / 格式化 / 状态机）
├── sdkconfig.defaults  默认配置（UTF-8、CJK 字体、NimBLE 外设）
└── partitions.csv      分区表（NVS + PHY + 单一 factory 应用）
```

## 构建

```bash
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o FoloToy-AI-Passport-full.bin
```

需要 ESP-IDF **5.5.3**。完整说明（含刷机与字体重新生成）见 [../docs/03-build.md](../docs/03-build.md)。

## 运行时约束

- **LVGL 非线程安全**：BLE 回调和按键回调都不直接操作对象；按键回调需先 `bsp_lvgl_lock()`。
- **状态锁只保护数据**：渲染前先拷贝快照并释放锁，避免持锁期间绘图。
- **按键回调不阻塞**：提示音走独立任务（`nav_ui.c` 里的 `beep_task`）。
- **`nav_app_t` 必须是 static**：曾因放在 `app_main` 栈上导致返回后指针悬垂、反复崩溃。

## 测试

`tests/` 下的测试不依赖 ESP-IDF 与 LVGL：

```bash
cc -std=c11 -Wall -Wextra -Werror -Imain \
   tests/test_nav_protocol.c main/nav_protocol.c -o /tmp/t && /tmp/t
```

## BLE 协议

见 [../docs/02-ble-protocol.md](../docs/02-ble-protocol.md)。当前帧格式 **v0.3（14 字段）**，与 `android/app/.../NavFrame.kt` 严格对应。
