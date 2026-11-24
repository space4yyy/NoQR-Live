# OBS QR Shield / 二维码自动遮挡

面向 macOS 和 Windows 的 OBS 原生视频滤镜，用于 Switch / Switch 2 采集卡直播等场景。macOS 使用 Apple Vision，Windows 使用 ZXing‑C++。插件在本地检测同屏多个二维码，按每帧的实际位置添加遮挡，不上传画面，也不记录二维码内容。

0.2.0 已将遮挡移到 GPU：每帧源纹理保留在显卡上，CPU 检测后只返回帧编号、矩形及成功/失败状态。GPU 在对应的原始纹理上一次性绘制黑块，再输出已遮挡纹理，不再将整帧像素重新上传到显卡。待检测纹理使用有限大小的复用池，旧设置、旧分辨率的结果不能引用已复用纹理。检测所需的 GPU → CPU 整帧读回仍保留，因此这项改动不能保证达到 60 fps。

0.4.0 固定使用原始分辨率检测，移除缩小检测选项，旧版本的缩小设置也会被忽略。支持自定义遮挡图片，同屏多个二维码分别覆盖同一张图片。Vision 请求、序列处理器、色彩空间及像素缓冲继续复用。

0.6.0 的 **遮挡方式**为：黑块、**高斯模糊 / Blur**、自定义图片。Blur 对原始二维码区域进行横向、纵向两次高斯模糊，保留原画的模糊外观。默认保留黑块；已有自定义图片配置会自动保留图片模式。

## 安装使用

| 平台 | 构建产物 | OBS 基线 |
| --- | --- | --- |
| macOS 13+，Apple Silicon | DMG 或 PKG | OBS 32.0.2 |
| Windows x64 | ZIP | OBS 32.0.2 x64 |

从 GitHub Actions 的 **Build OBS plugin** 运行详情下载对应平台的构建产物。动手安装前先退出 OBS。

- **macOS**双击 PKG 安装到 `~/Library/Application Support/obs-studio/plugins/obs-qrcode.plugin`。DMG 内含 PKG、安装说明和源码。安装包未经过 Developer ID 签名或 Apple 公证，通过互联网分享时可能被 Gatekeeper 拦截。 [Apple 官方说明](https://support.apple.com/zh-cn/102445)
- **Windows**解压 ZIP，将其中的 `obs-qrcode` 目录复制到 `%ProgramData%\obs-studio\plugins`。确认 DLL 的完整路径为 `%ProgramData%\obs-studio\plugins\obs-qrcode\bin\64bit\obs-qrcode.dll`。系统可能要求管理员权限。

1. 重启 OBS，在来源列表中右键采集卡来源 → **滤镜** → **效果滤镜** → `+` → **二维码自动遮挡 / QR Shield**。
2. 在 OBS **设置 → 高级 → 视频**中使用 SDR 设置，例如 NV12、Rec.709。采集卡也需输出 SDR，HDR 来源会输出黑屏。
3. 先本地录制，检查二维码从不同位置突然出现、移动和消失时的效果，再用于直播。

卸载时关闭 OBS，移除上面安装的插件目录。

## 参数

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| 额外遮挡边距 | 16 像素 | 实际边距取这个值与检测框边长 12% 中的较大者，覆盖框选偏差和边缘 |
| 画面缓冲 | 100 ms | 以采集时间计算的最低等待时间；实际延迟还受检测耗时和 OBS 调度影响 |
| 最高检测帧率 | 30 | 限制处理开销；不是实际帧率保证，可设置为 5–60 |
| 遮挡方式 | 黑块 | 可选黑块、高斯模糊 / Blur、自定义图片 |
| 模糊强度 | 80 | 范围 60–100，越高越模糊，低于 60 的配置会被强制提高到 60 |
| 自定义遮挡图片 | 留空 | 留空使用黑块，可选择 PNG / JPG 等静态图片 |

在滤镜属性中将 **遮挡方式**设为 **自定义图片**，再选择 **自定义遮挡图片**。每个二维码先铺不透明黑底，再等比完整显示图片；透明区域和四周均保留黑底，防止露出二维码。图片在后台解码并缓存，最长边限制为 1024 像素，只在选图或点击 **重新加载图片** 时加载，不会每帧读文件。文件内容替换后可点该按钮刷新；清空路径恢复黑块，加载失败时也使用黑块。动态图片仅显示第一帧。

将 **遮挡方式**设为 **高斯模糊 / Blur** 后可调整强度。强度下限为 60，并按每个遮挡区域的尺寸计算模糊半径：最低档高斯标准差约为各轴区域尺寸的 14%，最高档约为 30%。GPU 先裁剪到 64×64 局部缓冲，再以固定间距进行横向和纵向各 97 采样的高斯处理，最后平滑绘制回原区域；只处理二维码附近，检测仍保持原分辨率。模糊初始化或绘制失败时阻断该帧输出。旧版 `mask_mode=1` 现在对应真实 Blur，`mosaic_strength` 会迁移到 `blur_strength`。

真实模糊仍然包含原图信息，强度下限不是“所有扫码器均无法识别”的保证。检测漏报也仍可能发生；需要完全遮盖时使用黑块或自定义图片。

每张输出画面都来自已完成检测的同一张原始画面，遮挡坐标与画面严格对应。检测速度不足时跳过部分输入帧，并重复最后一张已处理画面，不把旧坐标直接套到新画面。OBS 即便设为 60 fps，实际内容更新速度仍可能低于 60 fps。

启动、修改设置或分辨率变化时会短暂黑屏；检测报错的帧整体涂黑。没有新结果时先保持上张已处理画面，超过 1.5 秒变为黑屏。队列有内存上限，不会无限积压。最大支持 4096 × 4096，超出时输出黑屏。

**检测成功并不意味着没有漏报。** 极小、模糊、遮挡、特殊样式或动画中的二维码仍可能漏检。本插件不能保证零泄露。关闭或移除此滤镜后，OBS 将显示原始来源。

滤镜只处理视频。请在 **高级音频属性 → 同步偏移**中按录制结果延迟采集卡声音；可从约 `+100 ms` 开始调整。游玩时优先使用 HDMI 直通显示器，避免受 OBS 预览延迟影响。完整分辨率的同步显卡读回仍有开销，图片遮挡功能不等于解决渲染耗时。

## 构建

### macOS

需要 OBS.app 和 Xcode Command Line Tools。编译后也可双击项目里的 `安装插件.command` 安装插件。

```sh
bash scripts/build.sh
bash scripts/install.sh
```

生成 PKG、DMG、对应源码 ZIP 和 SHA-256 校验文件：

```sh
bash scripts/package-macos.sh
```

PKG 使用当前用户安装域，不包含安装前后执行的 shell 脚本。插件固定安装在 `~/Library/Application Support/obs-studio/plugins/obs-qrcode.plugin`。包内约束为 Apple Silicon、macOS 13+，并提示安装前退出 OBS；不更改任何 OBS 场景。分享独立 PKG 时请一并提供对应源码 ZIP，或者直接分享包含源码的 DMG。

OBS 不在 `/Applications/OBS.app` 时：

```sh
OBS_APP="/自定义路径/OBS.app" bash scripts/build.sh
```

首次构建会下载并校验 OBS 32.0.2 和 SIMDe 0.8.2 头文件。`build/obs-qrcode.plugin` 使用本机架构并做 ad-hoc 签名，PKG 和 DMG 仅为 arm64 打包。

### Windows

在 Windows x64 上安装 CMake 3.28+ 和 Ninja，然后在仓库根目录运行：

```powershell
.\scripts\package-windows.ps1
```

脚本下载并校验官方 OBS x64、LLVM‑MinGW 和 ZXing‑C++，构建 DLL，运行模块加载、二维码坐标和图片解码测试，再将 ZIP 和 SHA-256 校验文件写入 `build/windows-x64/`。安装 ZIP 内附对应源码和许可证。

### GitHub Actions

[Build OBS plugin](.github/workflows/build.yml) 在 push、pull request 和手动触发时分别在 macOS arm64 和 Windows x64 runner 上执行。构建文件上传到该次运行的 Artifacts。

## 验证

```sh
bash scripts/test.sh       # Apple Vision 检测、坐标映射、遮挡、边界处理
bash scripts/test-obs.sh   # 独立 libobs 实例加载插件并实际渲染移动二维码
bash scripts/test-obs.sh 1920 1080 # 1080p、多二维码和正常背景检查
bash scripts/test-obs.sh --failure # 独立测试模块模拟检测报错，要求全程黑屏
QR_TEST_DETECTION_EDGE=1280 bash scripts/test-obs.sh 1920 1080
QR_TEST_GRAPHICS=metal QR_TEST_DETECTION_EDGE=960 bash scripts/test-obs.sh 1920 1080
QR_TEST_OVERLAY=fixture bash scripts/test-obs.sh 1920 1080
QR_TEST_GRAPHICS=metal QR_TEST_OVERLAY=fixture QR_TEST_OVERLAY_AFTER=build/missing.png bash scripts/test-obs.sh 1920 1080
QR_TEST_MODE=1 QR_TEST_BLUR_STRENGTH=0 bash scripts/test-obs.sh 1920 1080 # 验证强制最低60
build/blur-test --positive build/blur-positive.png build/blur-output-0.png build/blur-output-1.png build/blur-output-2.png
QR_TEST_MODE=1 QR_TEST_BLUR_STRENGTH=100 bash scripts/test-obs.sh 1920 1080
bash scripts/benchmark.sh # 仅 Vision 的合成图片基准，排除模型预热和图片缩放耗时
bash scripts/verify-blur.sh # 重新构建并验证最低/最高 Blur 的实际渲染输出与扫码结果
```

测试只使用 Core Image 自动生成的二维码，不读取采集卡或现有场景，不需要录屏权限。需正常的 macOS 图形会话和 Vision 服务访问；受限沙箱可能无法初始化 OpenGL 或加载 Vision 模型。默认测试 OpenGL；可用 `QR_TEST_GRAPHICS=metal` 切换本机 OBS 的 Metal 后端。`--failure` 链接专用的失败检测器，只生成单独的测试模块，不会覆盖正常插件。

0.2.0 在本机验证：OpenGL 1080p 输出 239 帧、Metal 640×360 输出 240 帧，两个测试都实际出现了正常背景、单个及多个黑块、二维码消失和设置切换，未检测到二维码模块泄漏；检测错误注入测试输出 239 帧，全部为黑屏。这是合成测试覆盖范围，不代表实际游戏画面不会漏检。

0.3.0 补充验证：1080p 原画在 OpenGL 的 1280 检测档、Metal 的 960 检测档各输出 239 帧，均通过移动、小二维码、多个二维码、切回原始检测分辨率及遮挡消失检查；检测错误注入测试输出 240 帧，全部为黑屏。坐标单元测试还覆盖奇数尺寸、竖屏、不放大小图以及映射时向外取整。

0.4.0 验证：OpenGL 原分辨率 1080p 已通过同屏两个二维码显示自定义图片、透明 PNG 黑底、清空图片和缺失文件回退测试；图片单元测试覆盖方向、透明度、缺失文件及 1024 像素尺寸限制。配置图片时的检测错误注入测试也通过，仍然全屏黑色。此版本的 Metal 测试在插件加载前初始化失败，未能完成，因此自定义图片功能本次仅验证 OpenGL。

0.6.0 验证：OpenGL 1080p 下最低阈值（故意传入 0，强制提高到 60）及最高强度 100 各输出 239 帧，均通过双二维码 Blur、平滑度、二维码消失和切回黑块检查。每档先确认原始图可识别两个二维码，再扫描三张实际 GPU 模糊输出，六张输出均未被 Vision 解码。原有图片模式和检测错误阻断也通过。这只覆盖合成样本和 Vision，不能代表所有二维码或扫码器。

检测测试覆盖无二维码、左上/右下位置变化、多二维码、1080p 小二维码和 4K。集成测试检查突然换位置、同时出现多个二维码、无二维码画面和运行时修改设置，逐帧检查二维码区域的白色模块是否泄漏，并要求正常背景、两个位置的遮挡、同屏双遮挡和遮挡消失都实际出现，防止全程黑屏或旧遮挡残留被误认为成功。

M2 上 0.3.0 的一次合成基准如下；每档排除前两次预热，统计十次检测，同一张 1080p 图片包含两枚二维码，再缩放为各检测尺寸。三个档位均检出 2/2。这里测量 Vision，不包含 OBS 显卡读回、缩放和推流，不能当作直播帧率保证。

| 检测尺寸 | 检测中位耗时 | P90 耗时 |
| --- | --- | --- |
| 1920×1080 | 47.92 ms | 60.41 ms |
| 1280×720 | 36.89 ms | 42.31 ms |
| 960×540 | 33.54 ms | 38.93 ms |

同一基准下，旧版 0.2.0 的 1080p 中位耗时为 69.85 ms。这是 0.3.0 的历史测试，会受系统负载和画面内容影响。0.4.0 已禁用缩小检测，上表的缩小档位仅作为历史记录，不是当前推荐设置。`QR_TEST_DETECTION_EDGE` 用于确认旧版本保存的缩小设置不会改变当前检测分辨率。

## 结构

- `src/core/` 存放坐标映射、遮挡区域和模糊参数等共享逻辑。
- `src/obs/` 存放 OBS 帧捕获、后台工作线程、GPU 遮挡和滤镜注册。
- `src/platform/macos/` 使用 Vision 检测二维码、ImageIO 解码遮挡图片。
- `src/platform/windows/` 使用 ZXing‑C++ 检测二维码、WIC 解码遮挡图片。
- `tests/` 存放离线检测、图像解码和 OBS 渲染测试。
- `config/versions.json` 集中记录版本和依赖校验值，`scripts/` 和 `packaging/` 存放构建与安装文件。

[OBS 图形 API](https://docs.obsproject.com/graphics) · [Apple Vision](https://developer.apple.com/documentation/vision/vndetectbarcodesrequest) · [ZXing‑C++](https://github.com/zxing-cpp/zxing-cpp)

项目源码采用 GPL-2.0-or-later，见 [LICENSE](LICENSE)。静态链接 Apache-2.0 ZXing‑C++ 的 Windows 二进制按 GPL-3.0-or-later 分发。 [GPL-3.0](packaging/GPL-3.0.txt)
