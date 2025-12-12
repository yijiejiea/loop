# Loop Video Player 🎬

一个轻量级的悬浮视频循环播放器，基于 **FFmpeg** 实现，支持几乎所有视频格式，为后续音视频编解码扩展打下基础。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![C++](https://img.shields.io/badge/C++-23-orange)
![FFmpeg](https://img.shields.io/badge/FFmpeg-latest-red)

## ✨ 功能特性

- 🔄 **无限循环播放** - 视频自动循环
- 🎬 **全格式支持** - 基于 FFmpeg，支持 MP4、MKV、AVI、MOV、WebM、RMVB 等几乎所有格式
- 🎵 **音视频同步** - 精确的音视频同步播放
- 🚀 **软件解码** - 兼容性强，支持所有视频格式
- 📌 **窗口置顶** - 始终显示在最上层
- 🖱️ **自由拖动** - 点击窗口任意位置拖动
- 📂 **拖放文件** - 直接拖放视频文件到窗口播放
- 📐 **边缘调整大小** - 拖动窗口边缘调整尺寸
- 🔆 **透明度调节** - 50% ~ 100% 可调
- 🖥️ **双击全屏** - 双击切换全屏模式
- 🔧 **可扩展架构** - 便于后续添加音视频编解码功能
- 🖥️ **跨平台设计** - Windows (D3D11) / macOS (Metal) / Linux (OpenGL)

## 🛠️ 构建要求

### 依赖项

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | >= 3.20 | 构建工具 |
| Qt6 | 6.x | Core、Gui、Widgets、Multimedia |
| FFmpeg | 最新 | 音视频编解码核心 |
| C++ 编译器 | C++23 | MSVC 2022 推荐 |

### 下载 FFmpeg SDK

1. 访问 FFmpeg 下载页面：
   - https://github.com/BtbN/FFmpeg-Builds/releases

2. 下载最新版本（推荐下载 **shared** 版本）：
   - `ffmpeg-master-latest-win64-gpl-shared.zip`

3. 解压到项目的 `third_party/ffmpeg` 目录：

```
loop/
├── third_party/
│   └── ffmpeg/
│       ├── bin/
│       │   ├── avcodec-61.dll
│       │   ├── avformat-61.dll
│       │   ├── avutil-59.dll
│       │   ├── swscale-8.dll
│       │   └── swresample-5.dll
│       ├── include/
│       │   ├── libavcodec/
│       │   ├── libavformat/
│       │   ├── libavutil/
│       │   ├── libswscale/
│       │   └── libswresample/
│       └── lib/
│           ├── avcodec.lib
│           ├── avformat.lib
│           ├── avutil.lib
│           ├── swscale.lib
│           └── swresample.lib
```

## 🔨 构建步骤

### Windows (Visual Studio 2022)

```bash
# 1. 创建构建目录
mkdir build
cd build

# 2. 配置 CMake
cmake .. -G "Visual Studio 17 2022" ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.5.2/msvc2019_64" ^
    -DFFMPEG_SDK_PATH="../third_party/ffmpeg"

# 3. 构建
cmake --build . --config Release

# 4. 运行
cd Release
LoopVideoPlayer.exe
```

### 使用 Qt Creator

1. 打开 Qt Creator
2. `文件` → `打开文件或项目` → 选择 `CMakeLists.txt`
3. 配置 CMake：在项目设置中添加 `-DFFMPEG_SDK_PATH=../third_party/ffmpeg`
4. 构建运行

### 使用 Visual Studio

1. 打开 Visual Studio 2022
2. 选择 `打开本地文件夹` → 选择项目目录
3. Visual Studio 会自动检测 CMakeLists.txt
4. 配置 CMakePresets.json 中的路径
5. 构建并运行

## 📖 使用方法

### 启动

```bash
# 直接启动（右键打开文件）
LoopVideoPlayer.exe

# 命令行指定文件
LoopVideoPlayer.exe "D:\Videos\my_video.mp4"

# 或拖放文件到程序图标/窗口
```

### 操作

| 操作 | 说明 |
|------|------|
| 左键拖动 | 移动窗口 |
| 边缘拖动 | 调整大小 |
| 双击 | 全屏/窗口切换 |
| 右键 | 打开菜单 |
| 鼠标悬停 | 显示控制栏 |
| 拖放文件 | 直接打开视频 |

### 右键菜单

- 📂 **打开视频文件** - 选择视频
- ▶ **播放/暂停/停止** - 播放控制
- 🔆 **透明度** - 调整窗口透明度
- 📐 **窗口大小** - 预设尺寸
- 📌 **始终置顶** - 切换置顶
- ❌ **退出** - 关闭程序

## 📁 项目结构

```
loop/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 说明文档
├── src/
│   ├── main.cpp                # 程序入口
│   ├── FloatingVideoPlayer.h   # 悬浮窗口
│   ├── FloatingVideoPlayer.cpp
│   │
│   │ # ===== 跨平台渲染架构 =====
│   ├── VideoRendererBase.h     # 抽象基类（跨平台接口）
│   ├── VideoRendererFactory.cpp# 平台渲染器工厂
│   ├── D3D11Renderer.h         # Windows D3D11 渲染器
│   ├── D3D11Renderer.cpp
│   ├── OpenGLRenderer.h        # 跨平台 OpenGL 渲染器
│   ├── OpenGLRenderer.cpp
│   │
│   │ # ===== 旧版兼容 =====
│   ├── FFmpegPlayer.h          # FFmpeg 播放器核心
│   ├── FFmpegPlayer.cpp
│   ├── VideoWidget.h           # QPainter 渲染组件
│   └── VideoWidget.cpp
└── third_party/
    └── ffmpeg/                 # FFmpeg SDK (需自行下载)
        ├── bin/                # DLL 文件
        ├── include/            # 头文件
        └── lib/                # 库文件
```

## 🏗️ 架构说明

项目采用**跨平台分层架构**设计：

### 三线程解码架构 (v2.1.0+)

为解决硬件视频解码过快导致的音视频同步问题，采用三线程架构：

```
                        ┌─────────────────┐
                        │   Demux 线程    │
                        │ av_read_frame() │
                        │ (读取 Packet)   │
                        └────────┬────────┘
                                 │
              ┌──────────────────┴──────────────────┐
              ↓                                     ↓
    ┌─────────────────────┐              ┌─────────────────────┐
    │  Video Packet 队列   │              │  Audio Packet 队列   │
    │  (最大 60 个)        │              │  (最大 120 个)       │
    └─────────┬───────────┘              └─────────┬───────────┘
              ↓                                     ↓
    ┌─────────────────────┐              ┌─────────────────────┐
    │  视频解码线程        │              │  音频解码线程        │
    │  D3D11VA 硬解码      │              │  FFmpeg 软解码       │
    │  ⚡ 超快，独立运行   │              │  🔊 独立运行        │
    └─────────┬───────────┘              └─────────┬───────────┘
              ↓                                     ↓
    ┌─────────────────────┐              ┌─────────────────────┐
    │  Video Frame 队列    │              │  Audio Data 队列    │
    │  (最大 10 帧)        │              │  (最大 100 帧)      │
    └─────────────────────┘              └─────────────────────┘
              ↓                                     ↓
         渲染定时器                            音频定时器
         (8ms 间隔)                           (5ms 间隔)
```

**优势：**
- ✅ 视频解码再快也不会阻塞音频解码
- ✅ 音频解码独立运行，保证时钟稳定
- ✅ Packet 队列作为缓冲，平滑处理速度差异
- ✅ 各线程互不干扰，资源利用率高

### 跨平台渲染架构

```
┌─────────────────────────────────────────────────────────────┐
│                    VideoRendererBase                        │
│              (抽象基类，定义通用接口)                        │
├─────────────────────────────────────────────────────────────┤
│  openFile() / play() / pause() / stop() / seek()            │
│  setVolume() / setDecodeMode() / setLoop()                  │
│  signals: positionChanged, endOfFile, errorOccurred...      │
└─────────────────────────────────────────────────────────────┘
              ▲                ▲                ▲
              │                │                │
     ┌────────┴───┐   ┌───────┴────┐   ┌───────┴────┐
     │ D3D11      │   │  Metal     │   │  OpenGL    │
     │ Renderer   │   │  Renderer  │   │  Renderer  │
     │ (Windows)  │   │  (macOS)   │   │  (Linux)   │
     │ D3D11VA    │   │VideoToolbox│   │VAAPI/VDPAU │
     └────────────┘   └────────────┘   └────────────┘
```

### 软硬解码选择

```cpp
// 设置解码模式
renderer->setDecodeMode(D3D11Renderer::Auto);      // 自动（优先硬件）
renderer->setDecodeMode(D3D11Renderer::Hardware);  // 强制硬件解码
renderer->setDecodeMode(D3D11Renderer::Software);  // 强制软件解码

// 查询当前解码状态
bool hw = renderer->isHardwareDecoding();
```

| 场景 | 推荐模式 | 原因 |
|------|----------|------|
| 日常播放 | Auto | 自动选择最优方案 |
| 4K/高码率 | Hardware | CPU 解不动 |
| 需要逐帧处理 | Software | 需访问原始像素 |
| 硬解出问题 | Software | 兼容性回退 |

### 核心类说明

| 类名 | 职责 |
|------|------|
| `VideoRendererBase` | 抽象基类，定义跨平台视频渲染接口 |
| `D3D11Renderer` | Windows 平台渲染器，D3D11VA 硬件加速 |
| `OpenGLRenderer` | 跨平台渲染器，支持 VAAPI/VideoToolbox |
| `FloatingVideoPlayer` | 主窗口，处理用户交互 |

## 🎬 支持的视频格式

基于 FFmpeg，支持：

| 格式 | 说明 |
|------|------|
| MP4, M4V | 最常用 |
| MKV | 支持多音轨/字幕 |
| AVI | 传统格式 |
| MOV | QuickTime |
| WebM | 网页视频 |
| WMV | Windows Media |
| FLV | Flash 视频 |
| RMVB, RM | RealMedia |
| TS, M2TS | 蓝光/广播 |
| 3GP | 手机视频 |
| ...更多 | FFmpeg 支持的都行 |

## 🔧 默认设置

| 设置 | 默认值 |
|------|--------|
| 窗口大小 | 400 × 300 |
| 窗口位置 | 屏幕右下角 |
| 透明度 | 95% |
| 音量 | 50% |
| 循环 | 无限循环 |

## 🚀 后续扩展计划

由于采用 FFmpeg 作为后端，可以方便地扩展以下功能：

- [ ] **硬件加速解码** - 使用 DXVA2/NVDEC/VAAPI
- [ ] **视频转码** - 格式转换、压缩
- [ ] **视频滤镜** - 亮度、对比度、裁剪等
- [ ] **字幕支持** - 内嵌/外挂字幕
- [ ] **截图功能** - 截取视频帧
- [ ] **GIF 导出** - 视频片段转 GIF
- [ ] **音频提取** - 提取音轨
- [ ] **播放速度** - 倍速播放
- [ ] **快捷键支持**
- [ ] **系统托盘图标**

## ❓ 常见问题

### Q: 找不到 FFmpeg DLL？

A: 确保以下 DLL 在可执行文件同目录下：
- avcodec-xx.dll
- avformat-xx.dll
- avutil-xx.dll
- swscale-x.dll
- swresample-x.dll

或者将 FFmpeg 的 bin 目录添加到系统 PATH。

### Q: 视频无法播放？

A: 
1. 检查文件路径是否正确（避免中文路径问题）
2. 确认文件未损坏
3. 检查 FFmpeg DLL 是否完整
4. 查看调试输出的错误信息

### Q: 视频卡住/音频不走？

A: 可能是音视频同步死锁导致。v2.1.0 修复了此问题：
1. 检测音频队列状态
2. 当音频断粮时，强制渲染视频以打破死锁
3. 确保解码线程不会因为视频队列满而永久阻塞音频解码

### Q: 音视频不同步？

A: 当前版本使用**音频主时钟**同步策略 (Video Sync to Audio)：
1. 当视频超前音频时，自动等待
2. 当视频落后音频时，自动丢帧追赶
3. 支持 SDL3 精确音频时钟

如果仍有问题：
1. 尝试重新打开视频
2. 使用 seek 跳转后等待同步

### Q: 如何添加硬件加速？

A: 可以在 `DecodeThread::openFile()` 中添加硬件解码器初始化：
```cpp
// 尝试硬件解码
AVBufferRef *hw_device_ctx = nullptr;
if (av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0) >= 0) {
    m_videoCodecCtx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
}
```

## 📝 版本历史

### v2.1.0 (当前)
- 🚀 **三线程解码架构** - 彻底解决音视频不同步问题
  - Demux 线程：专门读取 Packet 并分发
  - 视频解码线程：独立硬件解码，不阻塞音频
  - 音频解码线程：独立软解码，保证时钟稳定
- ⚡ 引入 Packet 队列作为缓冲层
- 🔧 优化 seek 和循环播放逻辑

## 🔧 本次调优（2025-12-12）
- Qt 音频输出支持分段写入，避免部分写导致的音频截断和失真
- 音量缩放只执行一次，防止重复缩放带来的失真和响度异常
- 循环开始时等待音频预热（约 200ms 或最多 500ms），避免新一轮循环开头画面先行导致的音画错位/卡顿感
- 循环首帧前暂缓音频输出，待首帧视频就绪或超时（500ms）再放行，避免“音频抢跑、视频追赶”
- 循环切换时先 flush 并等待音/视频/packet 队列全部清空，再 seek 回头，保证上一轮尾帧不会被提前覆盖

### v2.0.0
- 🔄 从 libmpv 切换到 FFmpeg
- ✨ 新增拖放文件功能
- 🏗️ 重构为分层架构
- 📝 完善文档

### v1.0.0
- 🎬 基于 libmpv 的初始版本
- ✨ 基本播放功能

## 📄 许可证

MIT License

---

**Made with ❤️ using Qt6, C++23, and FFmpeg**
