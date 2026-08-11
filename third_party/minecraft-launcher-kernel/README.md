# Minecraft Launcher Kernel

A lightweight, modular Minecraft launcher kernel written in C++17 with Qt 6. Supports full game launch pipeline: version resolution, asset/library download, mod loader installation, authentication, and mod searching.

一个轻量、模块化的 Minecraft 启动内核，使用 C++17 + Qt 6 编写。支持完整的游戏启动流程：版本解析、资源/库下载、模组加载器安装、认证和模组搜索。

---

## Features / 功能

- **Version Resolution** — Parse Minecraft version JSON with full inheritance chain (`inherits_from`)
- **Library & Asset Download** — Batch concurrent downloads with SHA1 verification, configurable mirror sources
- **Asset Index Resolution** — Parse asset indexes, download/verify individually
- **Native Library Extraction** — Auto-extract platform-specific natives from JARs
- **Classpath Construction** — Full classpath with inherited version support, module-path detection for Forge 1.17+
- **Game Argument Building** — Modern (`arguments.game`) and legacy (`minecraftArguments`) formats; correct handling of empty game arrays (Fabric/Forge)
- **Authentication** — Microsoft MSA (device code flow) + offline mode, session save/refresh
- **Mod Loader Installation** — Fabric, Forge (direct JSON profile), NeoForge, Quilt, OptiFine, LiteLoader
- **Mod Searching** — Modrinth API integration
- **Java Runtime Detection** — Scan and download Java runtimes
- **JSON Output Mode** — `--json` flag for machine-readable logging
- **Bilingual i18n** — Built-in translation system with 9 languages

---

## Build / 构建

### Prerequisites / 前置要求

- C++17 compiler (GCC 10+, Clang 12+, MSVC 2022 17+)
- Qt 6.5+ (Core, Network modules)
- CMake 3.16+ (recommended) or MinGW Make

### Build with CMake

```bash
# Configure
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="E:/Qt/6.11.1/mingw_64" \
  -DCMAKE_CXX_COMPILER=g++

# Build
cmake --build build -- -j$(nproc)

# Output: build/dist/*.exe
```

### Build with Makefile (legacy)

```bash
# Edit QT_DIR in Makefile to match your Qt installation
mingw32-make -j4
```

---

## Programs / 程序

| Program | Description | 说明 |
|---------|-------------|------|
| `mclaunch` | Launch a Minecraft version | 启动游戏版本 |
| `mcver` | List and inspect versions | 列出/查看版本 |
| `mcsearch` | Search available versions | 搜索可用版本 |
| `downloader` | Download client, assets, libraries, Java | 下载客户端/资源/库/Java |
| `installer` | Install mod loaders (forge/fabric/...) | 安装模组加载器 |
| `login` | Microsoft account authentication | Microsoft 账户认证 |
| `mcjava` | Scan and list Java runtimes | 扫描 Java 运行时 |
| `modsearch` | Search mods on Modrinth | 搜索模组 |
| `modver` | List mod versions and details | 查看模组版本详情 |

See `--help` on each program for usage.

---

## Architecture / 架构

```
launcher-kernel/
├── libmcbase/          # Core library (static): i18n, log, http, download, version, mod
├── mclaunch/           # Launcher: game argument building, classpath, JVM launch
├── downloader/         # Download CLI: assets, libraries, Java runtimes
├── installer/          # Mod loader installer CLI
├── login/              # Microsoft MSA authentication
├── mcver/              # Version listing and inspection
├── mcsearch/           # Version search
├── mcjava/             # Java runtime scanner
├── modsearch/          # Mod search (Modrinth)
├── modver/             # Mod version listing
├── lang/               # i18n translation files (en, zh, ja, ko, fr, de, es, pt, ru)
└── dist/               # Build output directory
```

Kernel modules are independent — removing any module leaves the core complete.

内核模块互不依赖——删除任意模块内核依然完整。

---

## Usage Examples / 使用示例

```bash
# Launch Minecraft 1.20.4 with Fabric
mclaunch 1.20.4 --java /path/to/java --memory 4096

# Launch with custom JVM arguments
mclaunch 1.20.1 --java /path/to/java --jvm "-XX:+UseG1GC -XX:MaxGCPauseMillis=200"

# Launch with session and debug output
mclaunch 1.20.4 --java /path/to/java --session session.json --debug

# Install Forge (direct profile download)
installer forge 1.20.1 --java /path/to/java

# Search mods on Modrinth
modsearch --platform modrinth sodium --mc-ver 1.20.1

# Login to Microsoft
login login --lang zh
```

---

## License / 许可证

BSD 3-Clause License — see [LICENSE](LICENSE).

This project links against Qt, which is available under LGPL v3 / GPL v2. Qt is not part of this project's license; refer to Qt's own licensing terms.

本项目链接 Qt（Qt 使用 LGPL v3 / GPL v2 授权）。Qt 不适用本项目的许可证，请参阅 Qt 自身的授权条款。

---

## Credits / 鸣谢

- [opencode](https://opencode.ai) — AI-powered coding assistant
- [DeepSeek](https://deepseek.com) — Large language model
- [Agnes](https://agnes-ai.com) — Large language model
- [MiniMax](https://www.minimaxi.com) — Large language model
