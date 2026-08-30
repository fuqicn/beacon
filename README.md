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
- Automatic update on Windows / Linux / macOS

## Supported Platforms

| Platform | Minimum Version | Maximum Tested | Architecture | Notes |
|----------|----------------|----------------|--------------|-------|
| Windows | 10 (1809) | 11 | x86_64 (amd64), ARM64 | Windows 7/8/8.1 **not supported** — Qt 6 requires Windows 10+ |
| Linux (deb) | Debian 13 / Ubuntu 25.10 / Ubuntu 26.04 LTS | — | x86_64, ARM64 | **Only** Debian 13, Ubuntu 25.10 and Ubuntu 26.04 LTS (or newer) ship Qt 6.8+. Older releases are **not supported**. Linux Mint latest is **not supported**. |
| Linux (rpm) | Fedora 40 | — | x86_64, aarch64 | Fedora 39 and below are **not supported** (lack Qt 6.8). RHEL / CentOS require manual compilation. |
| Linux (arch) | Arch Linux | — | x86_64, aarch64 | Rolling release; always has Qt 6.x |
| Linux (tar.gz) | Any glibc ≥ 2.31 | — | x86_64, aarch64 | Universal fallback; run `./Beacon` directly |
| macOS | 12 (Monterey) | 15 | ARM64 (native), x86_64 | macOS 11 and below **not supported** — Qt 6.8.3 requires macOS 12+ |

### Qt 6 version notes

| Distro | Default Qt 6 in repos |
|--------|----------------------|
| Debian 13 (trixie) | 6.8.2 |
| Ubuntu 25.10 (kinetic) | 6.8.3 |
| Ubuntu 26.04 LTS (lunar) | 6.10.2 |
| Fedora 40 | 6.8.2 |
| Fedora 41 | 6.8.x+ |
| Arch Linux | rolling (latest) |

## Download Mirrors

All Minecraft assets, libraries, and Java downloads route through mirrors defined in
`mirrors.json` (bundled with the launcher):

| Mirror | Minecraft Libraries / Assets | Modrinth | Java |
|--------|-----------------------------|----------|------|
| bmclapi | `bmclapi2.bangbang93.com` | ✅ | — |
| mcbbs | `download.mcbbs.net` | ✅ | — |
| mcimirror | `mod.mcimirror.top` | ✅ (full CDN) | — |
| adoptium (fallback) | — | — | OpenJDK binaries |

The launcher probes all mirrors at startup and picks the fastest. You can change
the source in **Settings → Downloads → Source**.

## Build

### Prerequisites

- C++17 compiler (GCC 10+, Clang 12+, MSVC 2022 17+)
- Qt 6.8+ (Core, Network, Quick, Qml, QuickControls2, QuickDialogs2, Svg, Widgets)
- CMake 3.16+
- zlib

```bash
cmake -B build -S .
cmake --build build --target Beacon
```

Or with the one-line build script:

```bash
python pack.py
```

The launcher kernel is bundled in `third_party/minecraft-launcher-kernel`.

### Linux packaging (one command)

```bash
# Debian / Ubuntu (arm64 on Apple Silicon Macs is also supported)
python pack.py --build-dir build-deb

# Fedora / RHEL
podman run -v $(pwd):/workspace -w /workspace fedora:40 \
  bash -c 'dnf install -y cmake ninja rpm-build qt6-qtbase-devel && python pack.py --build-dir build-rpm'

# Arch Linux
python pack.py --build-dir build-arch
```

Output packages:

| Format | File name | Install command |
|--------|-----------|----------------|
| Debian (.deb) | `BeaconLauncher-deb-amd64.deb` | `sudo dpkg -i BeaconLauncher-deb-amd64.deb` |
| RPM (.rpm) | `BeaconLauncher-redhat-x86_64.rpm` | `sudo dnf install BeaconLauncher-redhat-x86_64.rpm` |
| Arch (.pkg.tar.zst) | `BeaconLauncher-arch-x86_64.pkg.tar.zst` | `sudo pacman -U BeaconLauncher-arch-x86_64.pkg.tar.zst` |
| Generic tarball | `beacon-1.0.2-linux-x86_64.tar.gz` | Extract and run `./Beacon` |

## Data directories

| Platform | Launcher data dir |
|----------|------------------|
| Windows | `<exe>/../game/` (preserved across self-extractor updates) |
| Linux | `~/.local/share/beacon-launcher/` |
| macOS | `~/Library/Application Support/beacon-launcher/` |

Persistent files: `.minecraft/`, `auth/`, `cache/`, `.runtime/`, `settings.ini`.

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

## Thanks to

- opencode
- deepseek
- agnes
