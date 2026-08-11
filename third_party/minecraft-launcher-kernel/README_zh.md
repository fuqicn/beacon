# Minecraft Launcher Kernel

[English](README.md)

一个轻量、模块化的 Minecraft 启动内核，使用 C++17 + Qt 6 编写。支持完整的游戏启动流程：版本解析、资源/库下载、模组加载器安装、认证和模组搜索。

---

## 功能

- **版本解析** — 解析 Minecraft 版本 JSON，支持完整继承链（`inherits_from`）
- **库与资源下载** — 批量并发下载，SHA1 校验，可配置镜像源
- **资源索引解析** — 解析资源索引，逐个下载/校验
- **本地库解压** — 自动解压平台相关的 JAR 原生库
- **类路径构建** — 完整类路径，支持版本继承，Forge 1.17+ 模块路径检测
- **启动参数构建** — 现代（`arguments.game`）与旧式（`minecraftArguments`）格式；正确处理空游戏参数数组（Fabric/Forge）
- **认证** — Microsoft MSA（设备代码流）+ 离线模式，会话保存/刷新
- **模组加载器安装** — Fabric、Forge（直下 JSON profile）、NeoForge、Quilt、OptiFine、LiteLoader
- **模组搜索** — Modrinth API 集成
- **Java 运行时检测** — 扫描和下载 Java 运行时
- **JSON 输出模式** — 机器可读日志的 `--json` 标志
- **多语言 i18n** — 内置翻译系统，支持 9 种语言

---

## 构建

### 前置要求

- C++17 编译器（GCC 10+、Clang 12+、MSVC 2022 17+）
- Qt 6.5+（Core、Network 模块）
- CMake 3.16+（推荐）或 MinGW Make

### 使用 CMake 构建

```bash
# 配置
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.1/mingw_64" \
  -DCMAKE_CXX_COMPILER=g++

# 构建
cmake --build build -- -j$(nproc)

# 输出: build/dist/*.exe
```

### 使用 Makefile 构建（旧式）

```bash
# 修改 Makefile 中的 QT_DIR 以匹配你的 Qt 安装路径
mingw32-make -j4
```

---

## 程序

| 程序 | 说明 |
|------|------|
| `mclaunch` | 启动游戏版本 |
| `mcver` | 列出/查看版本 |
| `mcsearch` | 搜索可用版本 |
| `downloader` | 下载客户端/资源/库/Java |
| `installer` | 安装模组加载器 |
| `login` | Microsoft 账户认证 |
| `mcjava` | 扫描 Java 运行时 |
| `modsearch` | 搜索模组（Modrinth） |
| `modver` | 查看模组版本详情 |

每个程序的使用方式请参阅 `--help`。

---

## 架构

```
launcher-kernel/
├── libmcbase/          # 核心库（静态）：i18n、log、http、download、version、mod
├── mclaunch/           # 启动器：启动参数构建、类路径、JVM 启动
├── downloader/         # 下载 CLI：资源、库、Java 运行时
├── installer/          # 模组加载器安装 CLI
├── login/              # Microsoft MSA 认证
├── mcver/              # 版本列表与查看
├── mcsearch/           # 版本搜索
├── mcjava/             # Java 运行时扫描器
├── modsearch/          # 模组搜索（Modrinth）
├── modver/             # 模组版本列表
├── lang/               # i18n 翻译文件（en、zh、ja、ko、fr、de、es、pt、ru）
└── dist/               # 构建输出目录
```

内核模块互不依赖——删除任意模块内核依然完整。

---

## 使用示例

```bash
# 以 Fabric 启动 Minecraft 1.20.4
mclaunch 1.20.4 --java /path/to/java --memory 4096

# 使用自定义 JVM 参数启动
mclaunch 1.20.1 --java /path/to/java --jvm "-XX:+UseG1GC -XX:MaxGCPauseMillis=200"

# 使用会话和调试输出启动
mclaunch 1.20.4 --java /path/to/java --session session.json --debug

# 安装 Forge（直下 profile）
installer forge 1.20.1 --java /path/to/java

# 在 Modrinth 上搜索模组
modsearch --platform modrinth sodium --mc-ver 1.20.1

# 登录 Microsoft 账户
login login --lang zh
```

---

## 许可证

MIT License — 参见 [LICENSE](LICENSE)。

本项目链接 Qt（Qt 使用 LGPL v3 / GPL v2 授权，非纯 GPL）。Qt 为动态链接，不适用本项目的 MIT 许可证，请参阅 Qt 自身的授权条款。

---

## 鸣谢

- [opencode](https://opencode.ai) — AI 编程助手
- [DeepSeek](https://deepseek.com) — 大语言模型
- [Agnes](https://agnes-ai.com) — 大语言模型
- [MiniMax](https://www.minimaxi.com) — 大语言模型