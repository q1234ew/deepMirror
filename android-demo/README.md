# DeepMirror Android USB 连通性 Demo

第一阶段验证普通 Android 手机通过 USB OTG 访问 Insight 相机，不需要 Root。当前不采集 RGB、深度或点云，不修改相机配置。

## 手机上测试

1. 安装 `DeepMirror-Android-USB-Demo.apk`。这是开发测试 APK，首次安装时按手机提示允许相应来源安装。
2. 将相机切到 UVC 模式，等待启动，再用支持数据传输的 OTG 线连接手机。供电不确定时先使用独立供电 USB 3.x Hub。
3. 打开“DeepMirror USB 检测”，点击“刷新设备”。目标设备为 `VID_3652 / PID_0104`，会优先显示。
4. 选中相机，点击“连接并检测”。按系统提示允许相机权限和 USB 设备访问。相机权限是 Android USB 视频设备访问的前提之一，Demo 不打开内置摄像头。
5. 查看报告：设备是否打开、标准设备/配置描述符是否读取成功、是否出现 RGB/深度视频接口和端点，以及 XU GUID。
6. 点击“连续检测 30 秒”，保持 App 在前台、屏幕亮起。检测每秒读取设备描述符；可点击停止，拔出设备也会中止。
7. 点击“复制诊断报告”，粘贴回项目沟通中排查。

## 如何理解结果

- **未发现设备**：优先检查手机 USB Host、OTG 适配器、数据线、相机模式与供电。
- **USB 授权未完成**：点击连接重试，或检查 App 的相机权限。拔插后 USB 访问权限需要重新确认。
- **控制读取通过**：说明本次 USB 授权、设备打开和标准控制读取可用，不代表视频流已经接收成功。
- **XU GET_INFO / GET_LEN**：单独记录左目 `0x14`、RGB `0x16` 标定 selector 的只读能力探测。失败不会被当作基础 USB 连接失败；须在下一阶段进一步适配。
- **30 秒检测通过**：仅证明轻量控制读取在本次条件下持续可用，不是 USB 视频带宽或相机满负载供电测试。
- `bMaxPower` 是相机配置描述符声明的取电值，不是手机输出上限或电表实测电流。`bcdUSB` 是设备声明版本，不等于实际协商链路速度。

报告包含手机型号、系统版本、USB 设备与路径、接口和扩展单元信息。App 没有网络权限，不会自动上传。分享前可检查报告内容。

## Android Studio 构建

在 Android Studio 中打开本目录 `android-demo`，使用 Gradle JDK 17 或兼容的 Studio 自带 JDK，安装 Android SDK Platform 34 / Build Tools 34.0.0。

```sh
./gradlew :app:assembleDebug :app:check :app:lintDebug
```

若本机 Gradle 配置了代理但代理未启动，可仅在本次命令末尾追加 `-Dhttps.proxyHost= -Dhttp.proxyHost= -DsocksProxyHost=`；也可在 Android Studio 中检查代理设置。

输出：`app/build/outputs/apk/debug/app-debug.apk`。测试 APK 的应用 ID 为 `com.deepmirror.usbcheck`，版本为 0.1.0，最低 Android 8.0（API 26）。采用系统 Java UI 和 AndroidX Core 广播兼容层，不依赖桌面 Qt 或厂商 Windows SDK。

不同构建来源的开发签名可能不同；若更新安装提示签名冲突，请先卸载旧 Demo 再安装。

本机可运行 `scripts/build-android.sh`，会优先使用已安装 Android Studio 的 JDK。SDK 路径通过 `ANDROID_HOME` 或本地 `local.properties` 指定；个人路径不提交到 Git。

构建验证包含独立 JVM 描述符测试，覆盖 XU 字段与 GUID 字节序、MJPG 图像尺寸、端点、USB 2/3 功耗单位、短包和异常长度。没有实机测试记录时，不能将构建通过视为 Galaxy S23 接相机通过。
