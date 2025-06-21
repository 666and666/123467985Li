# C++ 音乐播放器 (v3.7 - 在线TTS最终版)

这是一个基于 C++ 开发的桌面音乐播放器，利用 ImGui 构建用户界面，miniaudio 进行音频处理，并集成了 `ixwebsocket` 库以支持在线文本转语音 (TTS) 功能。

## 目录

- [项目简介](#项目简介)
- [主要特性](#主要特性)
- [技术栈](#技术栈)
- [文件结构](#文件结构)
- [构建与运行](#构建与运行)
  - [先决条件](#先决条件)
  - [构建步骤](#构建步骤)
- [使用说明](#使用说明)
- [未来的改进](#未来的改进)
- [许可证](#许可证)

## 项目简介

本项目旨在创建一个功能完备、界面友好的本地音乐播放器，并在此基础上增加了在线文本转语音 (TTS) 功能，可以在播放歌曲前通过语音播报歌曲信息。它支持扫描本地音乐文件夹，播放常见的音频格式，并提供播放控制、音量调节和播放模式切换等功能。

## 主要特性

- **直观的用户界面**: 采用 ImGui 库构建，提供现代化的浅色主题。
- **本地音乐管理**:
  - 支持添加和管理多个音乐文件夹。
  - 自动扫描指定文件夹及其子文件夹中的 `.mp3`, `.wav`, `.flac` 格式音乐文件。
  - 自动从文件名中解析歌曲名和艺术家信息（如果文件名格式为 "艺术家 - 歌曲名"）。
- **音频播放控制**:
  - 播放/暂停、上一曲/下一曲。
  - 进度条拖动，实时显示播放进度和音量。
  - 音量调节。
  - 支持顺序播放、单曲循环和随机播放模式。
- **在线文本转语音 (TTS)**:
  - 集成了讯飞开放平台的在线 TTS 服务。
  - 在播放新歌曲之前，会通过 TTS 播报歌曲名称。
  - TTS 音频流通过 WebSocket 实时获取并播放。
- **跨平台支持**: 基于 SDL2, OpenGL 和 CMake 构建，理论上可支持多种操作系统。

## 技术栈

- **C++**: 主要开发语言。
- **ImGui**: 即时模式图形用户界面库，用于构建播放器的界面。
- **miniaudio**: 轻量级音频播放库，用于加载、解码和播放音频文件。
- **SDL2**: 简单直接媒体层，提供窗口管理、OpenGL 上下文和事件处理。
- **OpenGL**: 图形渲染 API，用于渲染 ImGui 界面。
- **cURL**: 用于 HTTP 请求（虽然在提供的 `main.cpp` 中 TTS 部分主要使用了 `ixwebsocket`，但 `CMakeLists.txt` 中仍链接了 `CURL`，可能用于其他潜在的 HTTP 请求）。
- **CMake**: 跨平台构建系统，用于管理项目编译。
- **C++ Standard Library**: 包括 `<iostream>`, `<string>`, `<vector>`, `<filesystem>`, `<algorithm>`, `<fstream>`, `<sstream>`, `<random>`, `<future>`, `<iomanip>`, `<ctime>` 等用于文件操作、字符串处理、集合操作、随机数生成、时间处理和异步编程。

## 文件结构

```
mp3-player/
├──.vscode/
│  └──settings.json               #vscode配置文件，用于定义文件类型关联，以便编辑器能够正确识别不同文件扩展名并应用相应的语言特性
├── music/                        # 音频文件 
└── mp3-player/
  ├── CMakeLists.txt              # CMake 构建脚本
  ├── main.cpp                    # 应用程序主源文件，包含UI逻辑、音频处理、TTS集成
  ├── vendor/                     # 第三方库源码
  │   ├── miniaudio/              # miniaudio 库
  │   └── imgui/                  # ImGui 库
  └── font.ttf                    # 字体文件 (需放置于此路径，否则中文显示异常)


**`main.cpp` 关键部分解释:**

- **`AudioState` 结构体**: 封装了音频播放相关的所有状态，包括 `miniaudio` 解码器、互斥锁、播放模式、播放历史、TTS 缓冲区等。
- **`data_callback(ma_device\* pDevice, void\* pOutput, const void\* pInput, ma_uint32 frameCount)`**: `miniaudio` 的核心音频回调函数，负责从解码器读取 PCM 数据并填充音频缓冲区。它智能地处理 TTS 播放和歌曲播放的切换。
- **`PlaySongAtIndex(...)`**: 封装了播放歌曲的逻辑，包括停止当前歌曲、初始化歌曲解码器、获取 TTS 音频（如果成功），并启动音频设备。
- **ImGui UI 函数**: `ShowLeftSidebar`, `ShowPlaylistWindow`, `ShowPlayerWindow` 等函数负责渲染播放器的各个界面元素，响应用户交互。
- **配置管理**: `SaveConfig` 和 `LoadConfig` 用于保存和加载音乐文件夹路径。
- **`ScanDirectoryForMusic`**: 递归扫描指定目录，查找支持的音乐文件。
- **`FilledSlider`**: ImGui 自定义控件，用于创建带有填充效果的进度条和音量条。
- **`SetModernDarkStyle`**: 定义了 ImGui 界面的颜色和样式，使其看起来更现代化和简洁。

**`CMakeLists.txt` 关键部分解释:**

- 定义了项目名称、C++ 标准。
- 使用 `find_package` 查找 SDL2, OpenGL, OpenSSL, CURL 和 ixwebsocket 等第三方库。
- 通过 `add_library(imgui ...)` 将 ImGui 的源文件编译成一个静态库。
- `add_compile_definitions(MINIAUDIO_IMPLEMENTATION)`: 告诉编译器 `miniaudio.h` 应该实现其内部定义，而不是仅仅作为头文件包含。
- `add_executable(mp3-player main.cpp)`: 定义主应用程序可执行文件。
- `target_link_libraries`: 将所有需要的库链接到 `mp3-player` 可执行文件。

## 构建与运行

### 先决条件

在构建项目之前，您需要安装以下依赖：

- **C++ 编译器**: 支持 C++17，例如 GCC, Clang, MSVC。
- **CMake**: 版本 3.10 或更高。
- **SDL2 开发库**: [SDL 官方网站](https://www.libsdl.org/)
- **OpenGL 开发库**: 通常随显卡驱动或系统开发工具包安装。
- **OpenSSL 开发库**: [OpenSSL 官方网站](https://www.openssl.org/)
- **cURL 开发库**: [cURL 官方网站](https://curl.se/libcurl/)
- **miniaudio**: 项目中已包含其代码。
- **ImGui**: 项目中已包含其代码。
- **字体文件**: 将 `font.ttf` (支持中文的字体，例如思源黑体、Noto Sans CJK SC 等) 放置在项目根目录下，否则中文显示可能为乱码。

**在 Linux/macOS 上安装依赖的示例 (Debian/Ubuntu)**:

```
sudo apt update
sudo apt install build-essential cmake libsdl2-dev libgl-dev libssl-dev libcurl4-openssl-dev
```

### 构建步骤

1. **克隆或下载项目**: 将 `mp3-player` 文件夹放置到您选择的目录。

2. **创建构建目录并进入**:

   ```
   mkdir build
   cd build
   ```

3. **运行 CMake 配置**:

   ```
   cmake ..
   ```

   如果您在 Windows 上使用 Visual Studio，可以指定生成器：

   ```
   cmake .. -G "Visual Studio 17 2022" # 根据您的VS版本调整
   ```

4. **编译项目**:

   ```
   cmake --build .
   ```

   这将在 `build` 目录下生成可执行文件 (例如 `mp3-player.exe` 或 `mp3-player`)。

## 使用说明

1. **运行播放器**: 在 `build` 目录下找到生成的可执行文件并运行。
2. **添加音乐文件夹**: 在播放器界面的左侧，有一个“音乐文件夹路径”区域。在输入框中输入您的音乐文件夹路径（例如 `/home/user/Music` 或 `C:\Users\User\Music`），然后点击“扫描音乐”按钮。播放器将扫描该文件夹及其子文件夹中的音乐文件。
3. **播放列表**: 扫描完成后，所有找到的歌曲将显示在播放列表区域。双击任何歌曲即可播放。
4. **播放控制**:
   - **播放/暂停**: 点击中间的“播放”或“暂停”按钮。
   - **上一曲/下一曲**: 使用左右箭头按钮切换歌曲。
   - **进度条**: 拖动进度条可以快进/快退。
   - **音量**: 拖动音量条调节音量。
   - **播放模式**: 点击“顺序/单曲/随机”按钮切换播放模式。