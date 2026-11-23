#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""导航副屏 BLE 诊断工具。

依赖:  pip install bleak

用法:
    python3 tools/ble_probe.py                  # 扫描、连接、列出服务、读 STAT
    python3 tools/ble_probe.py --demo           # 再写入一条演示帧
    python3 tools/ble_probe.py --frame "NAV|..." # 写入指定帧
    python3 tools/ble_probe.py --no-connect     # 只扫描，不连接

用途:
    - 确认卡片在广播、服务已注册（自定义服务缺失会表现为连上即断，reason=531）
    - 不装手机 App 也能验证卡片解析与界面：写入一帧后看屏幕变化
"""
import argparse
import asyncio
import sys

SERVICE_UUID = "6e40ab00-b5a3-f393-e0a9-e50e24dcca9e"
NAV_CHAR_UUID = "6e40ab01-b5a3-f393-e0a9-e50e24dcca9e"
STAT_CHAR_UUID = "6e40ab02-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME = "PassportNav"

DEMO_FRAME = "NAV|1|1|CP1|2000|15|12000|2800|6000|900|23584|400|364,752|\n"


async def scan(timeout):
    from bleak import BleakScanner

    print(f"扫描 {timeout:.0f} 秒，查找 {DEVICE_NAME} ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME, timeout=timeout
    )
    if dev is None:
        print("未发现设备。请确认卡片开机、未被其他设备占用（一次只允许一个连接）。")
        return None
    print(f"发现: {dev.address} ({dev.name})")
    return dev


async def run(dev, frame):
    from bleak import BleakClient

    async with BleakClient(dev, timeout=30.0) as client:
        print(f"已连接: {client.is_connected}")
        for service in client.services:
            print(f"  服务 {service.uuid}")
            for ch in service.characteristics:
                print(f"    特征 {ch.uuid}  {ch.properties}")

        try:
            value = await client.read_gatt_char(STAT_CHAR_UUID)
            text = bytes(value).decode("utf-8", "replace")
            print(f"STAT = {text!r}" + ("  (空——已知问题，见 docs/05-faq.md)" if not text else ""))
        except Exception as exc:  # noqa: BLE001
            print(f"读 STAT 失败: {exc}")

        if frame is not None:
            payload = frame.encode("utf-8")
            await client.write_gatt_char(NAV_CHAR_UUID, payload, response=False)
            print(f"已写入 {len(payload)} 字节:")
            print("  " + frame.rstrip("\n"))
            print("请查看卡片屏幕是否按该帧渲染。")

        await asyncio.sleep(1.0)


def main():
    parser = argparse.ArgumentParser(description="导航副屏 BLE 诊断")
    parser.add_argument("--demo", action="store_true", help="写入一条演示帧")
    parser.add_argument("--frame", default=None, help="写入指定帧（自动补结尾换行）")
    parser.add_argument("--no-connect", action="store_true", help="只扫描不连接")
    parser.add_argument("--timeout", type=float, default=15.0, help="扫描超时秒数")
    args = parser.parse_args()

    frame = None
    if args.frame:
        frame = args.frame if args.frame.endswith("\n") else args.frame + "\n"
    elif args.demo:
        frame = DEMO_FRAME

    try:
        dev = asyncio.run(scan(args.timeout))
    except ModuleNotFoundError:
        print("缺少 bleak: pip install bleak")
        return 1
    if dev is None:
        return 2
    if args.no_connect:
        return 0
    try:
        asyncio.run(run(dev, frame))
    except Exception as exc:  # noqa: BLE001
        print(f"连接或写入失败: {exc}")
        print("若错误码为 531/133，通常是卡片的自定义服务未注册或已被其他设备占用。")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
