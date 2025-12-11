# Loop Video Player 🎬

一个轻量级的悬浮视频循环播放器，基于 **libmpv**（FFmpeg），支持几乎所有视频格式。

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![Qt](https://img.shields.io/badge/Qt-6.x-green)
![C++](https://img.shields.io/badge/C++-23-orange)
![mpv](https://img.shields.io/badge/mpv-libmpv-red)

## ✨ 功能特性

- 🔄 **无限循环播放** - 视频自动循环
- 🎬 **全格式支持** - 基于 FFmpeg，支持 MP4、MKV、AVI、MOV、WebM、RMVB 等几乎所有格式
- 🚀 **硬件加速** - 自动使用 GPU 解码
- 📌 **窗口置顶** - 始终显示在最上层
- 🖱️ **自由拖动** - 点击窗口任意位置拖动
- 📐 **边缘调整大小** - 拖动窗口边缘调整尺寸
- 🔆 **透明度调节** - 50% ~ 100% 可调
- 🖥️ **双击全屏** - 双击切换全屏模式
- 📦 **部署简单** - 只需 `mpv-2.dll` 一个依赖

## 🛠️ 构建要求

### 依赖项

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | >= 3.20 | 构建工具 |
| Qt6 | 6.x | 只需 Core、Gui、Widgets |
| libmpv | 最新 | 视频播放核心 |
| C++ 编译器 | C++23 | MSVC 2022 推荐 |

### 下载 libmpv

1. 访问 libmpv 下载页面：
   - https://sourceforge.net/projects/mpv-player-windows/files/libmpv/

2. 下载最新版本（如 `mpv-dev-x86_64-20231231-git-xxxxxx.7z`）

3. 解压到项目的 `third_party/mpv` 目录：

```
loop/
├── third_party/
│   └── mpv/
│       ├── include/
│       │   └── mpv/
│       │       ├── client.h
│       │       └── render_gl.h
│       ├── libmpv.dll.a     (导入库)
│       └── mpv-2.dll        (运行时 DLL)
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
    -DMPV_SDK_PATH="../third_party/mpv"

# 3. 构建
cmake --build . --config Release

# 4. 运行
cd Release
LoopVideoPlayer.exe
```

### 使用 Qt Creator

1. 打开 Qt Creator
2. `文件` → `打开文件或项目` → 选择 `CMakeLists.txt`
3. 配置 CMake：在项目设置中添加 `-DMPV_SDK_PATH=../third_party/mpv`
4. 构建运行

## 📖 使用方法

### 启动

```bash
# 直接启动（右键打开文件）
LoopVideoPlayer.exe

# 命令行指定文件
LoopVideoPlayer.exe "D:\Videos\my_video.mp4"

# 或拖放文件到程序图标
```

### 操作

| 操作 | 说明 |
|------|------|
| 左键拖动 | 移动窗口 |
| 边缘拖动 | 调整大小 |
| 双击 | 全屏/窗口切换 |
| 右键 | 打开菜单 |
| 鼠标悬停 | 显示控制栏 |

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
│   ├── MpvWidget.h             # MPV 播放组件
│   └── MpvWidget.cpp
└── third_party/
    └── mpv/                    # libmpv SDK (需自行下载)
        ├── include/
        ├── libmpv.dll.a
        └── mpv-2.dll
```

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
| 硬件解码 | 自动 |

## ❓ 常见问题

### Q: 提示找不到 mpv-2.dll？

A: 确保 `mpv-2.dll` 在可执行文件同目录下，或已添加到系统 PATH。

### Q: 视频无法播放？

A: 
1. 检查文件路径是否正确
2. 确认文件未损坏
3. 查看调试输出的错误信息

### Q: 如何关闭硬件解码？

A: 在 `MpvWidget.cpp` 中修改：
```cpp
mpv_set_option_string(m_mpv, "hwdec", "no");
```

### Q: 如何调整播放速度？

A: 可以添加功能，使用 mpv 属性：
```cpp
mpv_set_property_double(m_mpv, "speed", 1.5);  // 1.5 倍速
```

## 📝 开发计划

- [ ] 播放列表支持
- [ ] 播放速度调节
- [ ] 快捷键支持
- [ ] 系统托盘图标
- [ ] 字幕支持
- [ ] 视频滤镜

## 🆚 与 Qt Multimedia 版本对比

| 特性 | Qt Multimedia | libmpv (当前) |
|------|--------------|---------------|
| 格式支持 | 有限（依赖系统） | 全格式 |
| 部署 | 复杂（多个 DLL + 插件） | 简单（1个 DLL） |
| 硬件解码 | 有限 | 完整 |
| 稳定性 | 一般 | 优秀 |
| 文件大小 | 较小 | mpv-2.dll ~70MB |

## 📄 许可证

MIT License

---

**Made with ❤️ using Qt6, C++23, and libmpv**
