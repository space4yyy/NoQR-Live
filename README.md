# OBS QR Shield / 二维码自动遮挡

面向 macOS 和 Windows 的 OBS 原生视频滤镜，适用于 Switch / Switch 2 采集卡直播等场景。完全在本地检测同屏多个二维码并按帧在 GPU 上遮挡，不上传画面，也不记录二维码内容。

- **检测后端**：macOS 使用 Apple Vision，Windows 使用 ZXing‑C++，固定原始分辨率检测。
- **遮挡方式**：支持**黑块**、**高斯模糊 / Blur** 与**自定义图片**。
- **帧级对齐**：输出画面与检测结果严格对应同一帧，不把旧坐标套到新画面；检测报错或超时（> 1.5s）自动黑屏。

## 安装与使用

安装包、源码及 SHA-256 校验文件请前往 [GitHub Releases](https://github.com/space4yyy/NoQR-Live/releases/latest) 下载。安装或卸载前请先退出 OBS。

| 平台 | 构建产物 | OBS 基线 | 安装路径 |
| --- | --- | --- | --- |
| macOS 13+ (Apple Silicon) | DMG / PKG | OBS 32.0.2 | `~/Library/Application Support/obs-studio/plugins/obs-qrcode.plugin` |
| Windows x64 | ZIP | OBS 32.0.2 x64 | `%ProgramData%\obs-studio\plugins\obs-qrcode` |

- **macOS**：双击 PKG 安装（DMG 内含 PKG 与源码）。安装包未公证，如被 Gatekeeper 拦截请参考 [Apple 官方说明](https://support.apple.com/zh-cn/102445)。
- **Windows**：解压 ZIP，将 `obs-qrcode` 目录复制到上述路径（确认存在 `obs-qrcode\bin\64bit\obs-qrcode.dll`，可能需要管理员权限）。
- **卸载**：退出 OBS 后删除对应的插件目录。

### 在 OBS 中启用

1. 右键采集卡来源 → **滤镜** → **效果滤镜** → `+` → **二维码自动遮挡 / QR Shield**。
2. 确保采集卡与 OBS（**设置 → 高级 → 视频**）均使用 **SDR**（如 NV12、Rec.709）；HDR 来源会输出黑屏。
3. 滤镜仅处理视频，建议在 **高级音频属性 → 同步偏移** 中按录制结果调整音频延迟（可从 `+100 ms` 开始）。
4. 直播前先本地录制，测试二维码出现、移动与消失时的遮挡效果。

## 参数说明

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| 额外遮挡边距 | 16 像素 | 实际边距取此值与检测框边长 12% 中的较大者 |
| 画面缓冲 | 100 ms | 按采集时间计算的最低等待时间（实际延迟还受检测耗时影响） |
| 最高检测帧率 | 30 | 可设 5–60，用于限制检测开销，非实际帧率保证 |
| 遮挡方式 | 黑块 | 可选黑块、高斯模糊 / Blur、自定义图片 |
| 模糊强度 | 80 | 范围 60–100，越高越模糊（低于 60 会强制提高到 60） |
| 自定义遮挡图片 | 留空 | 支持 PNG / JPG 等静态图（动图仅取首帧）；留空或加载失败回退黑块 |

**注意事项：**
- **无法保证零泄露**：极小、模糊、变形或动画中的二维码仍可能漏检；真实高斯模糊仍保留部分原图信息，如需彻底遮盖请使用**黑块**或**自定义图片**（自定义图片会先铺不透明黑底，替换文件后可点 **重新加载图片** 刷新）。
- **性能与分辨率**：最大支持 4096 × 4096（超出输出黑屏）；检测需 GPU → CPU 读回，算力不足时会跳帧并重复上一帧已处理画面。游玩建议使用 HDMI 直通显示器。
- 启动、修改设置或分辨率变化时会短暂黑屏；关闭或移除滤镜后将直接显示原始画面。

## 从源码构建

### macOS

需要已安装 OBS.app 与 Xcode Command Line Tools：

```sh
bash scripts/build.sh          # 编译（非默认路径可设 OBS_APP=/path/to/OBS.app）
bash scripts/install.sh        # 安装到当前用户的 OBS 插件目录
bash scripts/package-macos.sh  # 打包 PKG / DMG / 源码 ZIP 与 SHA-256
```

### Windows

在 Windows x64 上安装 CMake 3.28+ 和 Ninja 后运行：

```powershell
.\scripts\package-windows.ps1
```

脚本会自动下载校验依赖、编译 DLL、运行基础测试，并在 `build/windows-x64/` 生成安装 ZIP 与校验文件。

### 测试与发布

```sh
bash scripts/test.sh           # 离线检测、坐标映射与图片解码测试（macOS）
bash scripts/test-obs.sh       # 独立 libobs 渲染集成测试（支持 QR_TEST_GRAPHICS=metal）
bash scripts/verify-blur.sh    # 验证模糊渲染输出与扫码结果
```

推送与 `config/versions.json` 一致的标签（如 `v0.6.0`）会触发 [GitHub Actions](.github/workflows/build.yml) 自动构建双平台产物并发布 Release。

## 目录结构

- `src/core/`：坐标映射、遮挡区域与模糊参数等共享逻辑
- `src/obs/`：OBS 帧捕获、后台线程、GPU 遮挡与滤镜注册
- `src/platform/macos/`、`src/platform/windows/`：各平台检测与图片解码实现
- `tests/`、`scripts/`、`packaging/`、`config/versions.json`：测试、构建打包脚本与依赖版本清单

## 许可证

项目源码采用 [GPL-2.0-or-later](LICENSE)。静态链接 Apache-2.0 ZXing‑C++ 的 Windows 二进制按 [GPL-3.0-or-later](packaging/GPL-3.0.txt) 分发。
