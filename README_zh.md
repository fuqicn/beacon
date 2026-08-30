# Beacon

[简体中文](README_zh.md)

一个基于 Qt 6 Quick/QML 构建的跨平台 Minecraft 启动器，由 [minecraft-launcher-kernel](third_party/minecraft-launcher-kernel) 引擎驱动。

## 功能特性

- 完整的 Minecraft 启动流程（版本解析、资源/库下载）
- Mod 加载器安装（Fabric、Forge、NeoForge）
- Microsoft 账户认证（MSA）
- Mod / 整合包搜索与下载
- Java 运行时管理与自动下载
- 实例管理，支持每个实例独立设置
- 跟随系统主题
- Windows / Linux / macOS 自动更新

## 支持平台

| 平台 | 最低版本 | 最高测试版本 | 架构 | 备注 |
|----------|----------------|----------------|--------------|-------|
| Windows | 10 (1809) | 11 | x86_64 (amd64)、ARM64 | **不支持** Windows 7/8/8.1 — Qt 6 要求 Windows 10+ |
| Linux (deb) | Debian 13 / Ubuntu 25.10 / Ubuntu 26.04 LTS | — | x86_64、ARM64 | **仅** Debian 13、Ubuntu 25.10 和 Ubuntu 26.04 LTS（或更新版本）内置 Qt 6.8+。**不支持**更旧的发行版。**不支持**最新的 Linux Mint。 |
| Linux (rpm) | Fedora 40 | — | x86_64、aarch64 | **不支持** Fedora 39 及以下版本（缺少 Qt 6.8）。RHEL / CentOS 需要手动编译。 |
| Linux (arch) | Arch Linux | — | x86_64、aarch64 | 滚动更新；始终拥有 Qt 6.x |
| Linux (tar.gz) | 任意 glibc ≥ 2.31 | — | x86_64、aarch64 | 通用备用方案；直接运行 `./Beacon` |
| macOS | 12 (Monterey) | 15 | ARM64（原生）、x86_64 | **不支持** macOS 11 及以下版本 — Qt 6.8.3 要求 macOS 12+ |

### Qt 6 版本说明

| 发行版 | 仓库中的默认 Qt 6 版本 |
|--------|----------------------|
| Debian 13 (trixie) | 6.8.2 |
| Ubuntu 25.10 (kinetic) | 6.8.3 |
| Ubuntu 26.04 LTS (lunar) | 6.10.2 |
| Fedora 40 | 6.8.2 |
| Arch Linux | 滚动更新（最新版） |

## 下载镜像

所有 Minecraft 资源、库和 Java 下载均通过 `mirrors.json`（捆绑在启动器中）中定义的镜像路由：

| 镜像 | Minecraft 库 / 资源 | Modrinth | Java |
|--------|-----------------------------|----------|------|
| bmclapi | `bmclapi2.bangbang93.com` | ✅ | — |
| mcbbs | `download.mcbbs.net` | ✅ | — |
| mcimirror | `mod.mcimirror.top` | ✅（完整 CDN） | — |
| adoptium（备用） | — | — | OpenJDK 二进制文件 |

启动器会在启动时探测所有镜像并选择最快的。您可以在 **设置 → 下载 → 源** 中更改来源。

## 构建

### 前置条件

- C++17 编译器（GCC 10+、Clang 12+、MSVC 2022 17+）
- Qt 6.8+（Core、Network、Quick、Qml、QuickControls2、QuickDialogs2、Svg、Widgets）
- CMake 3.16+
- zlib

```bash
cmake -B build -S .
cmake --build build --target Beacon
```

或者使用单行构建脚本：

```bash
python pack.py
```

启动器内核捆绑在 `third_party/minecraft-launcher-kernel` 中。

### Linux 打包（一条命令）

```bash
# Debian / Ubuntu（Apple Silicon Mac 上的 arm64 也支持）
python pack.py --build-dir build-deb

# Fedora / RHEL
podman run -v $(pwd):/workspace -w /workspace fedora:40 \
  bash -c 'dnf install -y cmake ninja rpm-build qt6-qtbase-devel && python pack.py --build-dir build-rpm'

# Arch Linux
python pack.py --build-dir build-arch
```

输出包：

| 格式 | 文件名 | 安装命令 |
|--------|-----------|----------------|
| Debian (.deb) | `BeaconLauncher-deb-amd64.deb` | `sudo dpkg -i BeaconLauncher-deb-amd64.deb` |
| RPM (.rpm) | `BeaconLauncher-redhat-x86_64.rpm` | `sudo dnf install BeaconLauncher-redhat-x86_64.rpm` |
| Arch (.pkg.tar.zst) | `BeaconLauncher-arch-x86_64.pkg.tar.zst` | `sudo pacman -U BeaconLauncher-arch-x86_64.pkg.tar.zst` |
| 通用压缩包 | `beacon-1.0.2-linux-x86_64.tar.gz` | 解压后运行 `./Beacon` |

## 数据目录

| 平台 | 启动器数据目录 |
|----------|------------------|
| Windows | `<exe>/../game/`（在自解压更新后保留） |
| Linux | `~/.local/share/beacon-launcher/` |
| macOS | `~/Library/Application Support/beacon-launcher/` |

持久化文件：`.minecraft/`、`auth/`、`cache/`、`.runtime/`、`settings.ini`。

## 许可证

版权所有 (C) 2024-2026 fuqicn

本程序是自由软件：您可以根据自由软件基金会发布的 GNU 通用公共许可证的条款重新分发和/或修改它，无论是许可证的第 3 版，还是（根据您的选择）任何更新版本。

本程序分发时希望它会有用，但**没有任何担保**；甚至没有对适销性或特定用途适用性的暗示担保。有关完整文本，请参阅 [LICENSE](LICENSE) 文件中的 GNU 通用公共许可证全文。

捆绑的启动器内核（`third_party/minecraft-launcher-kernel`）根据 MIT 许可证授权。详情请参见 [NOTICE](NOTICE) 和 `third_party/minecraft-launcher-kernel/LICENSE`。

## 致谢

- opencode
- deepseek
- agnes