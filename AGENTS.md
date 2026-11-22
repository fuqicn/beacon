# Beacon Launcher — Development Notes

## 0. Hard Rules (explicit permission required)

- **Do not commit or push** unless the user explicitly asks. "Make the change" is not
  permission to commit.
- **Do not change the version number** (`1.0.3`) unless the user explicitly asks.
- **Do not modify anything under `third_party/minecraft-launcher-kernel/`** unless the
  user explicitly asks. This includes editing kernel sources/headers, replacing or
  updating the kernel, and deleting files. Treat the kernel as read-only by default.

---

## 1. Project Structure

```
E:\projects\launcher\
├── src/
│   ├── cpp/                     # Launcher-side C++ (not the kernel)
│   │   ├── main.cpp             # Startup, splash, single-instance guard, crash handlers,
│   │   │                        # stall watchdog, palette/theme init, QML engine setup
│   │   ├── KernelBridge.cpp/h   # Qt ↔ kernel singleton, managers, Q_INVOKABLE entrypoints
│   │   └── VersionGroupModel.cpp/h
│   ├── kernel/
│   │   ├── managers/            # Launch, Download, Auth, Java, Mod, Modpack, Install,
│   │   │                        # Instance, Settings, Skin, I18n, Search, Version managers
│   │   └── platform/            # WindowEffects.cpp/h (Windows compositor / title bar)
│   ├── qml/
│   │   ├── main.qml             # Root window, sidebar, page loader, kill button, overlays
│   │   ├── pages/               # LaunchPage, DownloadPage, SettingsPage, ToolsPage,
│   │   │                        # InstancesPage, InstanceSettingsPage, LogViewerPage
│   │   ├── components/          # Reusable controls and sub-pages (search, dialogs, cards)
│   │   ├── theme/               # AppTheme.qml (live Theme context property), Colors.qml
│   │   └── i18n/                # zh.json, en.json, ja.json, fr.json
│   └── icons/                   # SVG icons + instances/*.png
├── resources/                   # qml.qrc, app.rc, app.ico, app.icns
├── packaging/                   # Windows self-extract launcher (main.c, beacon.rc),
│   │                            # Linux launcher (beacon_gtk.c), debian/, rpm/, arch/ templates
├── third_party/minecraft-launcher-kernel/   # C++17 kernel (flat copy, no submodule)
├── pack.py                      # One-command build + package for Windows/Linux/macOS
├── CMakeLists.txt               # Qt6 + C++17 build; local output goes to build/
├── build/                       # Local CMake build (Ninja, not tracked)
├── game/                        # Dev data dir when running build/Beacon.exe (not tracked)
├── dist/                        # Packaged output (not tracked)
│   ├── beacon/Beacon.exe        # Deployed Windows app + Qt DLLs + resources
│   ├── game/                    # Packaged app's persistent data dir
│   ├── beacon.zip               # Windows installer payload
│   └── BeaconLauncher-*.exe     # Windows self-extracting installer
├── .tmp/                        # Local scratch: SVG collection, kernel clones, test assets
└── Untitled.svg / Untitled.ico  # Source artwork for the app icon
```

**Git repo root:** `E:\projects\launcher\.git` — not `E:\projects\` and not `E:\launcher\`.
The kernel is tracked as plain files (no submodule, no nested `.git`).

**`.tmp/` is user-owned scratch space.** It is gitignored but must be preserved. Never
delete the directory itself; only remove a specific file/subdirectory you created, and
only after checking it is yours. It currently holds the Font Awesome SVG collection,
kernel clones and test assets.

---

## 2. Toolchain

| Component | Local Windows value |
|-----------|---------------------|
| Qt | `E:\Qt\6.11.1\mingw_64` (Qt 6.11.1) |
| Compiler | MinGW-w64 13.1.0 at `E:\Qt\Tools\mingw1310_64` |
| CMake | `E:\Qt\Tools\CMake_64\bin\cmake.exe` |
| Ninja | `E:\Qt\Tools\Ninja\ninja.EXE` |
| Build dir | `E:\projects\launcher\build\` |
| Output exe | `build\Beacon.exe` (unstripped, for debugging) |
| Dist exe | `dist\beacon\Beacon.exe` (deployed by `pack.py`; currently not stripped) |

Project requirements: CMake ≥ 3.16, C++17, Qt ≥ 6.8
(Core, Network, Quick, Qml, QuickControls2, QuickDialogs2, Svg, Widgets).

CI uses Qt 6.8.3 (MSVC on Windows, clang on macOS) and the distro Qt 6.8+ on Linux.
Local MinGW builds and CI MSVC builds must both keep working.

**Never change the generator** — local `pack.py` reconfigures with Ninja. If
`build/CMakeCache.txt` came from another generator, delete `build/CMakeCache.txt`
before rebuilding (or use `--skip-build` when only repacking).

---

## 3. Build & Package Workflow

### Incremental build (development)

```powershell
# Beacon.exe must not be running, or the linker cannot replace the output file.
taskkill /F /IM Beacon.exe 2>$null

& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build 'E:\projects\launcher\build' `
    --target Beacon --config Release --parallel 16
```

QML-only or single-CPP changes usually take ~15–30 s. `--config Release` is harmless
with Ninja (single-config) and required for MSVC/VS generators.

### Full package

```powershell
cd E:\projects\launcher
python pack.py                 # configure + build + deploy + package
python pack.py --skip-build    # repack the existing build
```

Common `pack.py` options: `--build-dir`, `--dist-dir`, `--qt-dir`, `--jobs`,
`--skip-build`, `--version`, `--debug`, `--arch`, `--msvc`, `--vcpkg`,
`--osx-arch arm64|x86_64`, `--mac-dist-dir`, `--no-proxy`.

Default build dirs: Windows `build/`, Linux `build-linux/`, macOS `build-mac-arm64/`.

Outputs:

| Platform | Files |
|----------|-------|
| Windows | `dist/beacon/`, `packaging/beacon.zip`, `dist/beacon.zip`, `dist/BeaconLauncher-windows-<amd64\|arm64>.exe` |
| Linux | `dist/BeaconLauncher-deb-<arch>.deb`, `dist/BeaconLauncher-redhat-<rpm_arch>.rpm`, `dist/BeaconLauncher-arch-<pacman_arch>.pkg.tar.zst`; fallback `dist/beacon-<version>-linux-<arch>.tar.gz` |
| macOS | `dist/Beacon-<arm64\|x86_64>.app` (or `--mac-dist-dir`); binaries are thinned with `lipo` when available |

Exit code 0 = success. `pack.py` deploys Qt with `windeployqt` / `macdeployqt`,
copies `mirrors.json`, writes `version.txt`, then builds the self-extracting Windows
launcher through the `BeaconLauncher` CMake target.

### Quick run & data directories

```powershell
Start-Process 'E:\projects\launcher\build\Beacon.exe'
Start-Process 'E:\projects\launcher\dist\beacon\Beacon.exe'
```

`resolveLauncherDir()` in `src/cpp/main.cpp` decides where data lives:

- `BEACON_LAUNCHER_DIR=<dir>` → `<dir>/beacon`
- Windows → `<exe dir>/../game`; if that path is not writable (e.g. Program Files),
  falls back to `%LOCALAPPDATA%\Beacon`
- Linux → `~/.local/share/beacon-launcher`
- macOS → `~/Library/Application Support/beacon-launcher`

So a dev run from `build/` uses `E:\projects\launcher\game\`, while the packaged app
uses `E:\projects\launcher\dist\game\`. Persistent data survives updates; the
`beacon/` app directory is replaced.

Logs and settings (in the active data dir):

- `launcher.log` — `mc_info` / `mc_error` output
- `qtdebug.log` — all Qt/QML messages (installed unconditionally in `main.cpp`)
- `settings.ini` — launcher-wide settings
- `instances/<versionId>/settings.ini` — per-instance settings
- `mirrors.json` — optional mirror override. `main.cpp` looks in the data dir first,
  then `/usr/share/beacon` (Linux) or `Contents/Resources` (macOS); `pack.py` ships a
  copy beside the Windows executable.
- `cache/icons` — downloaded mod icons
- `crash.log` — Windows crash handler output, next to `Beacon.exe` (not in the data dir)

### Version bump

Keep these in sync when releasing:

- `project(Beacon VERSION ...)` in `CMakeLists.txt`
- `app.setApplicationVersion(...)` in `src/cpp/main.cpp`
- macOS bundle version properties in `CMakeLists.txt`
- `pack.py` reads the CMake project version by default (`--version` overrides it) and
  rewrites the version inside `packaging/main.c` / `packaging/beacon_gtk.c`

Current version: `1.0.3`.

---

## 4. Kernel (`third_party/minecraft-launcher-kernel`)

**Default rule: the kernel is read-only.** Do not edit, replace or delete anything
inside `third_party/minecraft-launcher-kernel/` without explicit permission from the
user. The update procedure below applies only when the user requests a kernel update.

The kernel is a **flat directory copy**, not a git submodule. Update procedure:

```powershell
# 1. Clone fresh into .tmp (create .tmp if needed; never delete .tmp itself)
New-Item -ItemType Directory -Force E:\projects\launcher\.tmp | Out-Null
git clone --depth 1 https://github.com/fuqicn/minecraft-launcher-kernel.git `
    E:\projects\launcher\.tmp\kernel-clone

# 2. Compare public headers against our call sites before replacing
#    (grep src/ for every mc_* symbol you use)

# 3. Replace the tracked directory
Remove-Item E:\projects\launcher\third_party\minecraft-launcher-kernel -Recurse -Force
Copy-Item E:\projects\launcher\.tmp\kernel-clone `
    E:\projects\launcher\third_party\minecraft-launcher-kernel -Recurse
Remove-Item E:\projects\launcher\third_party\minecraft-launcher-kernel\.git `
    -Recurse -Force -ErrorAction SilentlyContinue

# 4. Remove only the clone you created (do NOT remove .tmp)
Remove-Item E:\projects\launcher\.tmp\kernel-clone -Recurse -Force

# 5. Full rebuild
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build 'E:\projects\launcher\build' `
    --target Beacon --config Release --parallel 16
```

`CMakeLists.txt` lists kernel `.cpp` files explicitly in `KERNEL_SRC`. If the new
kernel adds/removes source files, update that list; otherwise the build fails at link
time or silently misses code.

Kernel modules present: `libmcbase`, `login`, `mcjava`, `downloader`, `mclaunch`,
`mcver`, `modver`, `modsearch`, `packsearch`, `rsrcsearch`, `shdsearch`, `dpsearch`,
`installer`, plus `lang/` resources and `mirrors.json`.

### Kernel API compatibility notes

- `mc_http_release_thread_resources()` is gone from the public header. TLS QNAM is a
  raw `thread_local` pointer and teardown no longer blocks. Do not call it from new
  code. `LegacyJavaManifestFetcher.cpp` keeps a local no-op macro for old code paths.
- `mc_java_download_manifest_legacy` was removed/renamed in the kernel. Our
  `LegacyJavaManifestFetcher.cpp` provides a local compatibility implementation;
  `LauncherMemoryOptimizer.cpp` still calls it. Keep that shim until the call site is
  migrated.
- `mc_http_sleep(int ms)` replaces `QThread::sleep()` in kernel-side code.
- `mc_http_post_json()` is the convenience POST wrapper.
- Kernel headers live under `third_party/minecraft-launcher-kernel/*/include/`; the
  includes are added by `CMakeLists.txt`, so launcher code includes `<mc_xxx.h>`.

---

## 5. Architecture Notes

### QML <-> C++ surface

`main.cpp` creates the engine and exposes these context properties:

- `kernel` — `KernelBridge` singleton (`Q_INVOKABLE` methods + manager properties)
- `Theme` — an `AppTheme.qml` object created manually with `QQmlComponent`, not a
  qmldir singleton
- `I18n` — `I18nManager`
- `isWin11`, `uiStyleName` — platform/style flags

Image providers: `image://tinted/<icon>/<r>/<g>/<b>` and `image://modicon/<base64-url>`.
`VersionGroupModel` is registered as `Beacon.VersionGroupModel` 1.0.

`KernelBridge` exposes managers directly as constant properties: `versionManager`,
`downloadManager`, `authManager`, `javaManager`, `modManager`, `searchManager`,
`installManager`, `modpackManager`, `launchManager`, `instanceManager`,
`settingsManager`, `skinManager`. QML connects to e.g. `kernel.launchManager` and
`kernel.modManager`.

### Launch flow

```
QML (LaunchPage) ──▶ KernelBridge::launchGame(memory)
                     ├── read java/memoryMode; apply per-instance launch/* settings
                     ├── auto mode → computeAutoMemoryMB()
                     ├── javaPath already set? ──▶ doAuthAndLaunch()
                     │   else JavaManager / JavaDownloadWorker
                     │        └── javaDownloaded → onJavaReady() → doAuthAndLaunch()
                     └── doAuthAndLaunch()
                          ├── offline fallback account if not logged in
                          ├── AuthManager::refreshBeforeLaunch()
                          └── LaunchManager::launch()
                               └── doVerifyAndLaunch()
                                    └── verify client / libraries / assets
                                         └── doLaunch() → QProcess(java.exe)
```

### Minecraft-running detection

- Dedicated `QThread` named `mcRunningPoller`, ticking every **15 s**.
- Windows: process snapshot → Java/`javaw.exe` + `EnumWindows` title check.
- Unix: `pgrep -f net.minecraft.client`.
- Changes are queued back to the GUI thread and emitted as
  `minecraftRunningChanged`.
- `launchManager.running` tracks only our launched process.
- `main.qml` computes `mcRunning = launchManager.running || kernel.minecraftRunning`.
- The kill button hides immediately on `launchManager.processFinished()`; the poller
  is only a safety net for externally started Minecraft.

### Settings persistence

- Launcher-wide: `<data dir>/settings.ini` (`QSettings` INI).
- Per-instance: `<data dir>/instances/<versionId>/settings.ini`.
- `SettingsManager::setInstance(id)` switches the active file; `endInstance()` returns
  to launcher-wide settings; `globalValue()` always reads launcher-wide settings.
- Keys use slash notation. Known keys include:
  `java/memory`, `java/memoryMode`, `java/foundPath`, `java/foundMajor`,
  `launch/memory`, `launch/jvmOptimize`, `launch/jvmArgs`, `launch/javaPath`,
  `launch/fullscreen`, `launch/resolutionW`, `launch/resolutionH`,
  `launch/isolation`, `launch/isolationOverride`, `launch/isolationPolicy`,
  `download/source`, `download/threads`, `mod/source`, `mod/curseforgeApiKey`,
  `language/index`, `theme/mode` (legacy `theme/colorScheme`),
  `ui/style`, `system/scrollbars`, `system/transparency`, `system/animations`,
  `update/auto`, `app/lastVersion`.
- Theme mode changes apply live. Language, UI style and animation changes show the
  restart-required dialog.

### Auto memory allocator

`KernelBridge::computeAutoMemoryMB()` uses the currently available RAM and three tiers:

```
Tier 1: target 2 GB,  efficiency 1.00  (baseline)
Tier 2: target 6 GB,  efficiency 0.70  (modded play)
Tier 3: target 12 GB, efficiency 0.40  (heavy modpacks)
effective = budget × efficiency
allocate  = min(incremental, effective)
budget   -= allocate / efficiency
Floor: minMB (1024 MB by default)
```

Called from `launchGame()` at launch time and from the Settings preview. Both paths
must use the same function so the preview matches reality.

### Startup / updates

- A lightweight `QWidget` splash appears before font/threadpool/mirror/kernel/QML init.
- Single-instance guard: named Windows mutex, or `QLocalServer` on Unix (stale sockets
  are probed and removed).
- Windows crash handler writes `crash.log` next to the executable.
- Update checks are triggered from QML, gated by `update/auto`;
  `KernelBridge::checkForUpdate()` / `downloadUpdate()` / `installPendingUpdate()`
  handle the platform-specific install (sidecar/pkexec/self-extract).

---

## 6. Debugging & Diagnostics

### Stall watchdog (`src/cpp/main.cpp`)

`InstrumentedApplication::notify()` plus a separate watchdog thread log GUI events
taking **≥ 150 ms** (`kSlowEventMs`). Example:

```
[SlowEvent] 8414ms thread=... receiver=QThread name=modMirrorWarmup type=52
[SlowEvent] 195ms  thread=... receiver=... type=52
```

Common event types: `52` = DeferredDelete, `77` = UpdateRequest, `50` = SockAct,
`43` = MetaCall. `[Stack]` blocks dump the GUI thread stack (module + offset);
`captureGuiStack()` can enumerate all threads. The watchdog also logs
`proc cpu +Xms, gui thread cpu +Yms` using `GetProcessTimes` / `GetThreadTimes`
(not `clock()`, which reports wall-clock on Windows).

### QML page lifetime

- Pages are lazy-loaded `Loader`s in `main.qml` (indices 0–6).
- The idle timer unloads inactive pages after **2 minutes**; DownloadPage (index 1)
  is kept alive so search/browse state survives navigation.
- Do **not** call `qmlCollectGarbage()` from the timer: `trimComponentCache()` +
  `collectGarbage()` blocks the GUI thread for tens of milliseconds. Let reference
  counting reclaim old pages.

### Logs

See §3 for the data-dir layout. `qtdebug.log` receives every Qt message, including
QML `console.log` and image-loading errors.

### After a kernel update

A full rebuild is safer than incremental:

```powershell
Remove-Item 'E:\projects\launcher\build\CMakeCache.txt' -Force -ErrorAction SilentlyContinue
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' -B 'E:\projects\launcher\build' `
    -S 'E:\projects\launcher' -DCMAKE_BUILD_TYPE=Release -G Ninja
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build 'E:\projects\launcher\build' `
    --target Beacon --config Release --parallel 16
```

---

## 7. i18n Rules

- All user-visible strings go through `I18n.tr("key")` in QML.
- Keys live in `src/qml/i18n/{zh,en,ja,fr}.json` and are embedded through
  `resources/qml.qrc` (`:/i18n/<lang>.json`).
- Add every new key to **all four** files in the same change. Fallback is:
  active language → `zh` → raw key. A missing key shows the key string (or Chinese
  fallback), which is a visible bug.
- Known gap: `ja.json` and `fr.json` currently have only ~296 keys while
  `zh.json` has ~422 and `en.json` ~420. Treat this as existing debt, but do not add
  new keys only to en/zh.
- Keys are dot-namespaced: `settings.memory`, `instance.jvmOptimize`,
  `modSearch.supportModrinth`, `nav.launch`, `clear`.
- `%1`, `%2` placeholders are plain strings in JSON; QML calls `.arg(...)`.
- Language index mapping in `I18nManager`: `-1` system, `0` zh, `1` en, `2` ja, `3` fr.
- `language/index` is read at `I18nManager` construction; language changes require a
  restart (the Settings page offers it via `restartDialog`).

---

## 8. QML / UI Conventions

- Use `I18n.tr("key")` for all user-visible text — never hardcode strings.
- Use `Theme` for colors and shapes: `Theme.onSurface`, `Theme.onSurfaceVariant`,
  `Theme.surfaceContainer`, `Theme.primary`, `Theme.hoverOverlay`,
  `Theme.shapeSmall/Medium/Large`. `palette.text` is still common in older pages.
- Theme colors come from `kernel.window` / `kernel.text` / `kernel.highlight` etc.,
  which notify through `paletteChanged`. `Theme.darkMode` follows `theme/mode`, with
  a system fallback.
- `kernel` is the bridge; connect to `kernel` for bridge signals and to
  `kernel.launchManager` / `kernel.modManager` for manager signals.
- `Component.onCompleted` is for one-time setup only; avoid heavy work (first-frame
  stall).

### Icons and tinting

- Icons live in `src/icons/` and must be registered in `resources/qml.qrc` under
  `/icons`. `AppIcon.iconName` maps to `/icons/<name>.svg`.
- `AppIcon` renders through `image://tinted/...`. An empty `tint` uses `palette.text`
  and makes the SVG single-colour. `tint` must be a hex or named color
  (`"#ffffff"`, `"white"`); **`Qt.color("rgb(...)")` is invalid here** and produces a
  null tint → invisible icon.
- Multi-colour artwork (the Beacon logo) must use a bare
  `Image { source: "qrc:/icons/beacon.svg" }`, not `AppIcon`.
- Sidebar icons are hand-written outline SVGs (`nav-play.svg`, `nav-download.svg`,
  `nav-gear.svg`, `nav-wrench.svg`, `nav-bars.svg`) with `fill="none"` and
  `stroke="currentColor"`. Use them **only** in the sidebar; other screens keep the
  normal solid SVGs.
- Other icon conventions:
  - refresh = `arrow-rotate-right.svg`
  - memory optimisation = `arrows-rotate.svg`
  - back navigation = `arrow-left-long.svg` (icon button, not a text arrow)
  - search clear = `circle-xmark.svg` (implemented once in `TextField.qml`)

### Sidebar (`main.qml`)

- Expanded 200 px, collapsed 56 px; row height 44 px; icon size 20 px.
- Width animates on `Layout.preferredWidth` + `width` so the content pane reflows
  during the animation.
- Each row: fixed icon at `x: 20`, label at `x: 52`; label hidden when collapsed.
- One accent only: the 3 px `Theme.primary` bar on the active row. Hover is a rounded
  neutral rectangle (`Theme.hoverOverlay`, `Theme.shapeMedium`); selection adds a 7%
  grey tint and bold text. Icon and text colours do **not** change on hover/selection;
  text is `Theme.onSurface`.
- Toggle button is anchored to the right edge so it does not drift during animation.
- `isCompact` (`width < 600`) switches to the bottom `TabBar`, but the window minimum
  width is 800, so compact mode is currently unreachable on desktop. If compact mode
  is needed, lower `minimumWidth` and re-test.

### Responsive layouts

- Minimum window: 800 × 500. After the sidebar that leaves roughly 552 px for content.
- Put `Layout.minimumWidth: 0` on the content pane so wide children cannot force the
  whole right side past the window edge.
- Search filter rows use `Flow` and wrap onto extra lines; do not put many fixed-width
  controls in a single `RowLayout`.
- Download mode tabs compute their width from the available space
  (`Math.max(70, Math.min(100, (width - 5 * 8) / 6))`) instead of hard-coded 100 px.
- Avoid fixed widths > ~500 px in page content; use `Layout.fillWidth` and elide text.

### Controls / dialogs

- Shared controls: `TextField.qml`, `Button.qml`, `Card.qml`, `Divider.qml`,
  `AppIcon.qml`, `OverlayScrollBar.qml`.
- `TextField.qml` keeps `root.text` in sync with the inner `TextInput` on every edit
  and shows a `circle-xmark` clear button whenever text is non-empty. All search pages
  use this component, so fixing it once covers mods, modpacks, resource packs,
  shaders and data packs.
- Custom dialogs use `QtQuick.Dialogs`; the restart prompt is a `Dialog` with
  standard buttons.
- Do **not** call `Qt.quit()` from QML — the app manages its own lifecycle.

---

## 9. Platform-Specific Notes

### Windows

- `dist/game/` is the persistent data dir; `dist/beacon/` is replaced on update.
- If the install dir is read-only, data falls back to `%LOCALAPPDATA%\Beacon`.
- `version.txt` is read from beside the executable (with fallbacks for macOS/Linux
  install locations), not from the data dir.
- The kill button uses `power.svg` with `tint: "#ffffff"` (solid primary background
  in both themes); it must stay white.
- `pack.py` passes `--no-translations --no-opengl-sw --no-system-d3d-compiler` to
  `windeployqt`, removes stray MinGW DLLs, copies zlib / VC runtime DLLs, and has an
  ARM64 manual Qt deployment fallback.

### Linux

- `pack.py` configures with `-DCMAKE_INSTALL_PREFIX=/usr -DLINUX_NO_CACHEGEN=ON`.
  The latter avoids a Qt 6.8 `qmlcachegen` crash caused by unresolved context
  properties (`kernel`, `Theme`, `I18n`) at cache-build time.
- Native packages are built from a `cmake --install` staging tree; missing
  `dpkg-deb` / `rpmbuild` / `makepkg` skips only that format, with a `tar.gz`
  fallback if none are available.
- Runtime data dir: `~/.local/share/beacon-launcher`.

### macOS

- `CMAKE_OSX_DEPLOYMENT_TARGET` is 12.0; CI builds ARM64 natively and x86_64 via
  cross-compilation, then `pack.py` thins universal binaries with `lipo`.
- `resources/app.icns` is copied into `Contents/Resources` by `pack.py`; the runtime
  window icon is not overridden on macOS (the bundle icon is used).
- Data dir: `~/Library/Application Support/beacon-launcher`.

---

## 10. Commit Conventions

- Conventional commits: `fix:`, `feat:`, `refactor:`, `chore:`.
- Multi-line messages: `-m 'subject' -m 'body'` (single quotes avoid PowerShell
  parsing issues with `"`).
- Commits are local-only — do **not** push unless explicitly asked.
- Do not commit build output, `dist/`, `game/`, logs, `settings.ini`, `mirrors.json`,
  `version.txt` or `.tmp/`.
- LF→CRLF warnings are cosmetic; Git auto-converts on checkout
  (`core.autocrlf=true` in this repo).

---

## 11. Common Pitfalls

| Symptom | Cause | Fix |
|---------|-------|-----|
| Linker: `cannot open output file Beacon.exe: Permission denied` | Beacon is still running | `taskkill /F /IM Beacon.exe` before building |
| CMake generator mismatch (“Does not match”) | Cache was created with another generator | Delete `build/CMakeCache.txt`, re-run `pack.py` |
| `mc_http_release_thread_resources: not declared` | Removed from the kernel | Remove calls; the local shim in `LegacyJavaManifestFetcher.cpp` is only for legacy code |
| `mc_java_download_manifest_legacy: not declared` | Removed from the kernel | Keep/use the local compatibility implementation in `LegacyJavaManifestFetcher.cpp` |
| QML shows a raw i18n key | Missing translation in the active language | Add the key to all four JSON files |
| New icon is blank | Not registered in `resources/qml.qrc` | Add `<file alias="name.svg">` under `/icons` |
| Icon is invisible | `tint: Qt.color("rgb(...)")` yields null | Use a hex/named colour, e.g. `"#ffffff"` |
| Beacon logo turns monochrome | Rendered through `AppIcon` | Use a bare `Image` with `qrc:/icons/beacon.svg` |
| Search box always searches empty text | `TextField.root.text` not synced from the inner `TextInput` | Keep the `onTextChanged` sync in `TextField.qml` |
| Right pane clipped on narrow windows | Wide fixed-width rows force the layout wider than the window | `Layout.minimumWidth: 0`, `Flow`, responsive tab widths |
| `beacon.zip` stale in the installer | `beacon.rc` embeds the zip by mtime | `pack.py` calls `os.utime(packaging/beacon.rc)` after packing |
| Linux app crashes/spins on startup | Qt 6.8 `qmlcachegen` cannot resolve context properties | Build with `-DLINUX_NO_CACHEGEN=ON` (`pack.py` does this) |
| Kill button disappears but poller still says running | Poller interval is 15 s | Also rely on `launchManager.processFinished()`; `killAllMinecraft()` force-clears the flag |
| PowerShell mangles Chinese/JSON | Console encoding | Read files with `Get-Content -Encoding utf8` or Python `io.open(..., encoding='utf-8')`; write JSON with `ensure_ascii=False` |
| Symbol/kernel errors after a kernel update | `KERNEL_SRC` list is manual | Update the source list in `CMakeLists.txt` and do a full rebuild |

---

## 12. Files to Edit (Quick Reference)

| Task | File(s) |
|------|---------|
| New setting / UI text | `src/qml/pages/SettingsPage.qml` + 4× `src/qml/i18n/*.json` |
| JVM args / memory / launch | `src/kernel/managers/LaunchManager.cpp`, `src/cpp/KernelBridge.cpp` |
| Instance-level settings | `src/qml/pages/InstanceSettingsPage.qml`, `SettingsManager.cpp` |
| Sidebar / navigation / kill button | `src/qml/main.qml` + `src/cpp/KernelBridge.cpp` |
| Icons / tinting | `src/icons/`, `resources/qml.qrc`, `src/qml/components/AppIcon.qml` |
| Search UIs | `src/qml/components/{ModsSearchPage,ModpackSearchPage,SearchResourcePage,TextField}.qml` |
| Background workers / watchdog | `src/cpp/main.cpp` + the relevant manager |
| Kernel API change | `third_party/minecraft-launcher-kernel/` (replace dir, update `KERNEL_SRC`) |
| Build / package / installers | `pack.py`, `CMakeLists.txt`, `packaging/` |
| Theme / colors / shapes | `src/qml/theme/AppTheme.qml`, `src/cpp/KernelBridge.cpp` |

---

*Last updated: 2026-11-22. Version: 1.0.3. Kernel: latest from
https://github.com/fuqicn/minecraft-launcher-kernel.git*
