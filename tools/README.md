# 开发辅助工具

| 脚本 | 用途 |
|---|---|
| `gpx_inspect.py` | 检查 GPX 能否被正确使用：轨迹点数、路线总长、命名航点（CP）判定、海拔与总爬升 |
| `ble_probe.py` | BLE 诊断：扫描卡片、列服务、读 STAT、写入测试帧（不装 App 也能验证固件） |

## gpx_inspect.py

```bash
python3 tools/gpx_inspect.py 路线.gpx
```

输出示例：

```
轨迹点    : 6667
路线总长  : 18.78 km
海拔范围  : 2760 ~ 3482 m，总爬升约 933 m

命名航点  : 2 个
  [排除] 起终点             里程   18768 m  偏离轨迹     2 m  ← 贴近起点/终点
  [CP  ] cp1/7.5           里程    7505 m  偏离轨迹     8 m

可用于显示的 CP 个数: 1
```

判定规则与 App 一致：名字含起终点关键词、或沿路线里程距起点/终点 ≤100m 的航点会被排除。

只依赖标准库。

> Windows 控制台默认是 GBK，中文可能显示为乱码。先执行 `chcp 65001`，或改用 PowerShell 7。

## ble_probe.py

```bash
pip install bleak

python3 tools/ble_probe.py          # 扫描 + 列服务 + 读 STAT
python3 tools/ble_probe.py --demo   # 额外写入一条演示帧，看卡片是否按帧渲染
```

写入演示帧后，卡片应该显示「CP1 / 2.0km / 偏离 15m / 剩余 12.0km / 海拔 2800m」，进度条填充 40%，并在约 36%/75% 处出现两个 CP 刻度。

> 卡片一次只允许一个连接。若手机 App 已连着，先点「停止」再运行本工具。
