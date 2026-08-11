# Minecraft Launcher Kernel

[简体中文](README_zh.md)

A lightweight, modular Minecraft launcher kernel written in C++17 with Qt 6. Supports the full game launch pipeline: version resolution, asset/library download, mod loader installation, authentication, and mod searching.

---

## Features

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

## Build

### Prerequisites

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

## Programs

| Program | Description |
|---------|-------------|
| `mclaunch` | Launch a Minecraft version |
| `mcver` | List and inspect versions |
| `mcsearch` | Search available versions |
| `downloader` | Download client, assets, libraries, Java |
| `installer` | Install mod loaders (forge/fabric/...) |
| `login` | Microsoft account authentication |
| `mcjava` | Scan and list Java runtimes |
| `modsearch` | Search mods on Modrinth |
| `modver` | List mod versions and details |

See `--help` on each program for usage.

---

## Architecture

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

---

## Usage Examples

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

## License

MIT License — see [LICENSE](LICENSE).

This project links against Qt, which is available under LGPL v3 / GPL v2 (not GPL-only). Qt is dynamically linked and not covered by this project's MIT license; refer to Qt's own licensing terms.

---

## Credits

- [opencode](https://opencode.ai) — AI-powered coding assistant
- [DeepSeek](https://deepseek.com) — Large language model
- [Agnes](https://agnes-ai.com) — Large language model
- [MiniMax](https://www.minimaxi.com) — Large language model