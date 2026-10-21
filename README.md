# DeepMirror Capture

Qt 6 Widgets / C++17 RGB-D 采集工作台。Mac 保留模拟模式；Windows 构建包含官方 SDK 后端，可选择真实设备。**Mac 模拟模式及 Windows 编译、离线测试、打包自检已验证；真实相机联机验收尚未完成。**

Windows 上接相机请按 [Windows 相机采集测试步骤](docs/windows-camera-test.md) 操作，包含开启深度、选择真实设备、预览与保存验证。

## Mac 构建与启动

需要 CMake 3.21+、C++17 编译器、Qt 6.4+（Widgets / OpenGLWidgets / Concurrent）。本机 Qt 为 6.8.2。

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build -j 4
open build/deepmirror.app
ctest --test-dir build --output-on-failure
```

Mac 不下载或编译 Windows SDK，无需 ROS、OpenCV 或相机驱动。

## Windows 构建

准备 Visual Studio 2022 C++ 桌面工具链、Windows SDK、CMake、Git，以及 Qt 6 **MSVC x64** kit。PowerShell 中运行（路径替换为实际 Qt 安装目录）：

```powershell
./scripts/build-windows.ps1 -QtRoot C:/Qt/6.8.2/msvc2022_64
```

脚本配置、编译、运行离线测试并执行 windeployqt；可执行程序在 `build-windows/Release/deepmirror.exe`。也可传 `-SdkRoot C:/src/insight-sdk` 使用已有官方仓库。

默认自动拉取官方 SDK 固定提交 `58395177ba3ecd077309e87549022fdcec3a9a4f`，仅构建核心 C++ 源码与 HIDAPI，不构建其 OpenCV 示例。现版本核心使用 Windows Media Foundation / DirectShow，无 FFmpeg 调用；因此本项目不链接或部署仓库附带的 FFmpeg。HIDAPI DLL 自动复制。离线使用时预先准备完整 SDK checkout。

### 已检查设备的 USB 适配

本机检查到的设备报告 `insight7`，VID/PID `3652:0104`，RGB 位于 MI_02，深度 MI_06，灰度 MI_04。RGB 配置为 MJPEG 1088×1920@30；USB 深度声明 Z16 544×642。

官方 SDK 默认 PID `0b5c`、深度/灰度复合节点，不能直接按原样识别本设备。`cmake/PrepareInsight.cmake` 对构建目录中的源码副本作有锚点检查的补丁：匹配 PID 0104 / RGB MI_02 / 深度 MI_06，只打开所需两路；深度单节点不再要求第二灰度流。另修正上游按逻辑相机编号缓存参数时的数组容量。不会修改上游 checkout。未在 Windows 实测，因此接口映射、驱动枚举和厂商 XU 标定支持仍须验收。

原版复合布局设备可使用 `-LegacyUsb`（对应 `INSIGHT_SPLIT_USB=OFF`）。需要重建以切换 USB 布局，不支持同时混用两类或多台相机。

SDK 仓库：https://github.com/LooperRobotics/insight-sdk

## 真实相机操作

1. Windows 连接相机，确认 UVC 模式、USB 3 数据线、Windows 相机访问权限，关闭占用相机的其他程序。
2. 根据实际需求在设备网页手动开启深度输出。本次检查时该开关为关闭。程序**不会调用深度开关、改写相机参数、升级固件或切换设备模式**。打开采集通道会协商 USB 传输格式。
3. 在工具中选择「Insight 相机 / Windows SDK」，点击启动。初始化、回调队列、配对、解码和点云生成在后台运行。
4. 读取左相机与 RGB 标定；校验尺寸、内参、P/K 一致性、外参父子坐标系及四元数。失败会提示并结束本次采集，不生成猜测标定。
5. 收到有效 RGB-D 对后显示三路预览并允许保存。若缺深度、断开连接或超过 3 秒无有效配对，会显示状态；数据中断后禁止保存过期帧并停止连续保存。恢复数据后可手动重新开启连续保存。
6. 暂停会等待 SDK 停止完成再允许切换数据源。关闭窗口会等待设备与保存任务结束，避免回调访问已释放对象。

当前真机配置请求 RGB MJPEG 1088×1920@30、深度 Z16 544×642@30；实际回调尺寸必须匹配标定。原版布局请求 544×640，支持无附加行或多两行的深度，拒绝其他尺寸/非紧密行跨度。其他设备需扩展配置与格式处理，而非默默缩放。

### 时间与几何约定

- SDK 图像时间戳可能来自硬件元数据，也可能回退到 Media Foundation，接口不暴露来源。本版**不声称硬件同步**。
- 用同一主机单调时钟记录回调到达时间，队列各最多 6 帧，按顺序一对一匹配，允许差值 ≤20 ms，过期帧丢弃；不能把该误差等同于曝光同步误差。
- 元数据记录 SDK 原始时间戳、到达时间、配对差、队列丢弃数、被拒绝对数和原始标定。队列丢弃数不是硬件丢帧数。
- 对 Z16 使用小端 16 位毫米数据；根据标定尺寸识别并剔除 2 行元数据。0 为无效。MJPEG 按官方示例处理 `TS__` 非标准 APP1 段。
- 深度反投影至左相机坐标系；RGB 外参按 ROS parent ← child 约定取逆，将左相机点投影至 RGB。使用 RGB 像素 z-buffer 排除明显遮挡；无对应颜色的点不写入彩色点云，但原始深度完整保存。
- 与官方对齐接口一致，假设输入是已去畸变图像且 P 与 K 一致。程序无法单凭标定证明当前固件的原始/去畸变模式，须在真机验收时确认。点云为单帧，不做世界坐标融合。

## 界面与保存

左侧 RGB / 深度上下预览，中间点云，右侧紧凑设置栏。拖动分隔条调整比例；底部日志默认折叠。

- 空格启动 / 暂停；Ctrl+S（Mac Command+S）保存当前组。
- 数据源仅在采集停止后可切换。Mac 构建只提供模拟源。
- 目标帧率控制界面轮询上限，不改硬件帧率。
- 连续保存按间隔抽样；后台只允许一项保存，忙时跳过请求，不是全帧无损录像。
- 点云拖动旋转、滚轮缩放、双击复位。预览每四点取一点；文件保存全部有效着色点。
- 保存失败停止连续保存并显示日志，关闭时等待当前任务。

默认保存到 `~/Pictures/DeepMirror`，可修改。每组独立文件夹：

| 文件 | 内容 |
|---|---|
| rgb.png | RGB 图像 |
| depth_mm.png | 16 位灰度 PNG，毫米，0 为无效 |
| depth_preview.png | 深度伪彩色图，仅供观察 |
| cloud.ply | ASCII 彩色点云，米，左相机坐标系 x 右 / y 下 / z 前 |
| metadata.json | 来源、两路时间戳、匹配时钟/误差、RGB 与深度尺寸、标定和计数 |

先写隐藏临时目录，完整写入后重命名。模拟场景为运动半球与背景，320×240，同帧同坐标系；其时间戳为主机生成时间。

## 验证范围

- `capture_roundtrip`：RGB 无损读回、PNG IHDR 16 位、深度逐像素无损、有效点数与 PLY 行数。
- `rgbd_processing`：有界队列、一对一配对/超差丢弃、元数据行裁剪、外参逆变换、颜色投影、异常输入与特殊 JPEG 段。
- 已在 Mac 对真实后端代码做官方 SDK 头文件接口语法检查；这不等于 Windows 平台编译或联机验证。

Windows 验收需覆盖：PID/接口识别、RGB/Z16 格式及有效行、标定与彩色边缘对齐、时间戳来源、实际接收率、深度开关关闭提示、拔插、长时保存、磁盘写满、停止与关闭。请保留日志和一组原始输出以诊断。

## GitHub 云端构建

`.github/workflows/windows-build.yml` 会在推送、PR 或手动触发后，在 Windows Server 2022 上安装 Qt 6.8.2 MSVC x64，编译、运行离线测试并打包。可在仓库 Actions → Windows build → 成功运行的 Artifacts 中下载 `DeepMirror-Windows-x64-编号`，解压后运行 `deepmirror.exe`。无需在测试电脑安装 Qt 或 Visual Studio。

构建会再对打包目录运行一次离线自检，并附带提交号与依赖版本。云端没有相机，所以成功仅表示构建、依赖部署和离线数据处理通过，不能替代 Windows 相机联机验收。
