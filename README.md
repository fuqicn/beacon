# Beacon

[简体中文](README_zh.md)

A cross-platform Minecraft launcher built with Qt 6 Quick/QML, powered by the
[minecraft-launcher-kernel](third_party/minecraft-launcher-kernel) engine.

## Features

- Full Minecraft launch pipeline (version resolution, asset/library download)
- Mod loader installation (Fabric, Forge, NeoForge)
- Microsoft account authentication (MSA)
- Mod / modpack search and download
- Java runtime management and auto-download
- Instance management with per-instance settings
- System-following theme
- Cross-platform: Windows, Linux, macOS

## Build

Prerequisites:

- C++17 compiler (GCC 10+, Clang 12+, MSVC 2022 17+)
- Qt 6.8+ (Core, Network, Quick, Qml, QuickControls2, QuickDialogs2, Svg, Widgets)
- CMake 3.16+
- zlib (or system zlib)

```bash
cmake -B build -S .
cmake --build build --target Beacon
```

Or

```bash
python pack.py
```

The launcher kernel is bundled in `third_party/minecraft-launcher-kernel`.

## Thanks to:
opencode
deepseek
agnes

## License

Copyright (C) 2024-2026 fuqicn

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
[LICENSE](LICENSE) file for the full GNU General Public License text.

The bundled launcher kernel (`third_party/minecraft-launcher-kernel`) is
licensed under the MIT License. See [NOTICE](NOTICE) and
`third_party/minecraft-launcher-kernel/LICENSE` for details.
