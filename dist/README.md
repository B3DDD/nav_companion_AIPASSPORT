# 发布产物

这里存放**当前版本**的可直接使用文件，方便在 GitHub 上以直链方式下载（国内可通过 jsDelivr 加速，见根目录 README 的下载说明）。

| 文件 | 用途 | 大小 |
|---|---|---|
| `folotoy-ai-passport-nav-v0.3.bin` | 合并固件（从 `0x0` 刷入） | 2,053,184 B |
| `nav-companion-v0.3.apk` | Android App（侧载安装） | 2,433,266 B |

## 校验

```bash
sha256sum -c SHA256SUMS.txt
```

Windows PowerShell：

```powershell
Get-FileHash .\nav-companion-v0.3.apk -Algorithm SHA256
```

对照 `SHA256SUMS.txt` 中的值即可。

## 约定

- 每次发版**覆盖**这两个文件，并把版本号写进文件名（`-vX.Y`）。
- 历史版本请到 [Releases](../../releases) 下载（tag 对应版本）。
- 固件是 **merged 镜像**，必须从 `0x0` 刷入；只刷 app 分区会启动失败。
