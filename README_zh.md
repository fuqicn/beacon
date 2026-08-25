# Beacon

一个基于 Qt 6 Quick/QML 的跨平台 Minecraft 启动器，由
[minecraft-launcher-kernel](third_party/minecraft-launcher-kernel) 内核驱动。

## 功能

- 完整的 Minecraft 启动流程（版本解析、资源/库下载）
- 模组加载器安装（Fabric、Forge、NeoForge）
- Microsoft 账号认证（MSA）
- 模组 / 整合包搜索与下载
- Java 运行时管理与自动下载
- 实例管理，支持每个实例独立设置
- 跟随系统的主题 / 透明效果设置
- 跨平台：Windows、Linux、macOS

## 构建

前置要求：

- C++17 编译器（GCC 10+、Clang 12+、MSVC 2022 17+）
- Qt 6.8+（Core、Network、Quick、Qml、QuickControls2、QuickDialogs2、Svg、Widgets）
- CMake 3.16+
- zlib（或系统自带 zlib）

```bash
cmake -B build -S .
cmake --build build --target Beacon
```

或者

```bash
python pack.py
```
启动器内核已随仓库打包在 `third_party/minecraft-launcher-kernel`。

## 鸣谢列表:
opencode
deepseek
agnes

## 许可证

版权所有（C）2024-2026 fuqicn

本程序为自由软件：你可以依据自由软件基金会发布的 GNU 通用公共许可证第三版
（或你选择的任何更高版本）的条款重新分发和/或修改它。

本程序的发布是希望它有用，但**不提供任何担保**；甚至没有默示的适销性或
特定用途的适用性担保。详情请参阅 GNU 通用公共许可证。

你应该已随本程序收到一份 GNU 通用公共许可证副本。完整的 GPL v3 文本见
[LICENSE](LICENSE)。

随附的启动器内核（`third_party/minecraft-launcher-kernel`）采用 MIT 许可证。
详见 [NOTICE](NOTICE) 与 `third_party/minecraft-launcher-kernel/LICENSE`。
