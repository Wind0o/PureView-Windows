<p align="center">
  <img src="resources/AppIcon.png" width="128" alt="PureView icon">
</p>

<h1 align="center">PureView for Windows</h1>

<p align="center">
  A fast, borderless, native image viewer for Windows 10/11.<br>
  原生、无边框、为极速翻图而生的 Windows 图片查看器。
</p>

<p align="center">
  <img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%2F11%20x64-0078D4">
  <img alt="Native C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C">
  <img alt="Direct2D" src="https://img.shields.io/badge/rendering-Direct2D-7C3AED">
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/License-MIT-22C55E"></a>
</p>

## 核心体验

PureView Windows 版延续 macOS 版的交互，不是网页套壳：

- 普通鼠标滚轮向上/向下：上一张/下一张图片。
- 按住 `Control` 滚轮：以鼠标指针为中心连续放大/缩小，最高 32 倍。
- 切换不同长宽比的图片时固定窗口左上角，只改变宽高，不再到处移动。
- 普通窗口按图片比例填满，没有黑边；底部不显示文件名或状态文字。
- `F11`/`F` 沉浸全屏：原图填充背景，前景完整显示。
- 后台预解码前方 4 张、后方 2 张；快速连滚时丢弃过期结果。
- 大图先按屏幕尺寸解码，放大到 160% 后再升级完整分辨率。
- 拖放打开、自然文件名排序、单实例转发、键盘翻图和缩放。
- 无广告、无账号、无遥测、无网络服务。

## 操作

| 输入 | 动作 |
| --- | --- |
| 滚轮上 / 下 | 上一张 / 下一张 |
| `Control` + 滚轮 | 以指针为中心缩放 |
| 方向键 / `Space` | 浏览当前文件夹 |
| `+` / `-` / `0` | 放大 / 缩小 / 适应窗口 |
| 放大后拖动 | 平移图片 |
| 双击 / `F11` / `F` | 沉浸全屏 |
| `Esc` | 复位缩放或退出全屏 |
| `Ctrl` + `O` | 打开图片 |
| 指针移到顶部 | 显示最小化、全屏、关闭按钮 |

## 下载与安装

从 [GitHub Releases](https://github.com/Wind0o/PureView-Windows/releases)
下载 `PureView-<version>-Windows-x64.zip`，解压后：

1. 双击 `install.cmd`。
2. 安装器会把程序复制到
   `%LOCALAPPDATA%\Programs\PureView`，并注册所有支持格式。
3. Windows 会打开“设置 → 应用 → 默认应用 → PureView”，在那里确认要交给
   PureView 的图片类型。

安装不需要管理员权限，也不会静默覆盖用户已有的默认应用选择。只想便携使用时，
直接运行 ZIP 里的 `PureView.exe` 即可。卸载请运行安装目录里的
`uninstall.cmd`。

> 公共发行版暂时没有商业代码签名证书，首次下载时 Windows SmartScreen
> 可能显示“未知发布者”。源码、CI 构建过程和 SHA-256 校验文件都在本仓库公开。

## 图片格式

Windows 自带 WIC 解码器可直接处理 BMP、GIF、ICO、JPEG、JPEG XR、PNG、TIFF、
DDS 等格式。PureView 也注册 WebP、HEIC/HEIF、AVIF、JPEG 2000/JPEG XL、
PSD、TGA、OpenEXR、HDR 和常见相机 RAW 扩展名；这些格式能否解码取决于系统中
是否安装了对应的 Windows Imaging Component 编解码器。

## 从源码构建

要求 Windows 10/11 x64、Visual Studio 2022 或更新版本、CMake 3.24+：

```powershell
git clone https://github.com/Wind0o/PureView-Windows.git
cd PureView-Windows
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\PureView.exe --self-test
```

## 性能设计

- Direct2D 使用 GPU 即时绘制，不在 UI 线程解码图片。
- 两到三个后台工作线程，交互请求优先于预取请求。
- 预览最长边按显示器像素的 1.6 倍对齐到 512，限制在 3072–6144。
- 缓存按物理内存的 5% 自动调整，限制在 384 MiB–1 GiB。
- 80ms 滚轮防连跳，兼顾快速切换和部分鼠标驱动拆分事件。
- 解码请求带代次编号，快速切换时过期结果不会覆盖当前图片。
- 设备丢失时自动重建 Direct2D 目标和位图。

## 默认应用说明

PureView 按 Windows 官方机制注册专用 ProgID、`OpenWithProgids`、
`RegisteredApplications` 和 `Capabilities`。现代 Windows 要求默认应用选择
由用户在系统界面完成，所以安装器只注册能力并打开 PureView 对应的默认应用页面。

## 发布

推送 `v*` 标签会在 GitHub 的 Windows Server 2025 Runner 上完成：

1. MSVC x64 编译；
2. 核心回归测试；
3. WIC/Direct2D 原生运行时自检；
4. 独立解压后的发布包复检；
5. ZIP 与 SHA-256 文件发布。

PureView is local-first and released under the [MIT License](LICENSE).
