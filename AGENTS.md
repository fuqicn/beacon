# Beacon Launcher — Development Notes

## 1. Project Structure

```
E:\launcher\launcher\
├── src/
│   ├── cpp/                     # C++ backend
│   │   ├── main.cpp             # Event loop, watchdog, stack capture, CPU monitoring
│   │   ├── KernelBridge.cpp/h   # Qt ↔ kernel bridge (singleton, Q_INVOKABLE entrypoints)
│   │   └── WindowEffects.cpp/h  # Windows compositor effects
│   ├── kernel/
│   │   └── managers/            # Domain managers (Launch, Download, Auth, Mod, Settings, …)
│   └── qml/
│       ├── main.qml             # Root window, kill button, mcRunning polling
│       ├── pages/               # SettingsPage, LaunchPage, InstanceSettingsPage, …
│       ├── components/          # Reusable QML (ModsSearchPage, DownloadStatusPanel, …)
│       └── i18n/                # en.json, zh.json, fr.json, ja.json
├── third_party/minecraft-launcher-kernel/  # C++17 kernel (libmcbase + modules)
├── packaging/                   # .spec, .iss, beacon.rc (embeds beacon.zip)
├── pack.py                      # Single-command build+package (see §4)
├── CMakeLists.txt               # Qt6 + MinGW build; outputs to build/
└── dist/
    ├── beacon/Beacon.exe        # Stripped binary + DLLs + resources
    ├── BeaconLauncher-*.exe     # Self-extracting installer (embeds beacon.zip)
    └── beacon.zip               # Payload for installer
```

**Git repo root:** `E:\launcher\launcher\.git` (NOT `E:\launcher\`). The kernel directory is tracked as plain files (no submodule, no `.git`).

---

## 2. Toolchain

| Component | Path / Value |
|-----------|-------------|
| Qt | `E:\Qt\6.11.1\mingw_64` (MinGW 13.1.0, Qt 6.11.1) |
| CMake | `E:\Qt\Tools\CMake_64\bin\cmake.exe` |
| Compiler | MinGW-w64 (bundled with Qt) |
| Generator | Ninja (via pack.py auto-detect) |
| Build dir | `E:\launcher\launcher\build\` |
| Output exe | `build\Beacon.exe` (unstripped, for debugging) |
| Dist exe | `dist\beacon\Beacon.exe` (stripped, shipped) |

**Never change generator** — `pack.py` re-configures with Ninja by default. If `CMakeCache.txt` exists from a previous generator run, delete `build/CMakeCache.txt` before rebuilding, or use `--skip-build`.

---

## 3. Build & Package Workflow

### Incremental build (during development)
```powershell
& 'E:\Qt\Tools\CMake_64\bin\cmake.exe' --build 'E:\launcher\launcher\build' `
    --target Beacon --config Release --parallel 16
```
Only recompiles changed files. ~15–30 s for QML-only or single-CPP changes.

### Full package (after all changes)
```powershell
cd E:\launcher\launcher
python pack.py                         # configure + build + zip + launcher
# or if CMake cache is already correct:
python pack.py --skip-build            # only pack, no cmake reconfigure
```
`pack.py` output: `dist/beacon.zip`, `dist/BeaconLauncher-windows-amd64.exe`. Exit code 0 = success.

### Quick run (during debug)
```powershell
Start-Process 'E:\launcher\launcher\dist\beacon\Beacon.exe'
# Logs: E:\launcher\launcher\dist\game\launcher.log
#        E:\launcher\launcher\dist\game\qtdebug.log
#        E:\launcher\launcher\dist\game\settings.ini
```
Data dir = `dist/game/` (persistent across updates; `dist/beacon/` is replaced on update).

---

## 4. Kernel (third_party/minecraft-launcher-kernel)

The kernel is a **flat directory copy** — not a git submodule. When updating:

```powershell
# 1. Clone fresh (depth 1, to temp)
git clone --depth 1 https://github.com/fuqicn/minecraft-launcher-kernel.git E:\launcher\.tmp\kernel-clone

# 2. Compare API headers against current usage
#   (grep our code for each exported symbol, verify they still exist)

# 3. Replace
Remove-Item E:\launcher\launcher\third_party\minecraft-launcher-kernel -Recurse -Force
Copy-Item E:\launcher\.tmp\kernel-clone E:\launcher\launcher\third_party\minecraft-launcher-kernel -Recurse

# 4. Remove .git if present
if (Test-Path 'third_party\minecraft-launcher-kernel\.git') {
    Remove-Item 'third_party\minecraft-launcher-kernel\.git' -Recurse -Force
}
Remove-Item E:\launcher\.tmp -Recurse -Force

# 5. Build to verify
& cmake --build build --target Beacon --config Release --parallel 16
```

### Kernel API compatibility notes (as of Sep 2026)
- `mc_http.cpp`: TLS QNAM changed from `thread_local unique_ptr<QNAM>` to `thread_local QNAM*`. **`mc_http_release_thread_resources()` removed from public API** — TLS teardown no longer blocks. Keep calls only as historical comments.
- `mc_java_download_manifest_legacy` → renamed to `mc_java_download_manifest`. Update all call sites.
- `mc_download_qt.h`: default concurrency comment updated 8→64 (matches `pack.py` config).
- `mc_http_sleep(int ms)` added — use instead of `QThread::sleep()` in C++ kernel code.
- `mc_http_post_json()` added convenience wrapper.
- Kernel CMakeLists uses `Qt6_FIND_COMPONENTS` — must link against same Qt 6.11.1 MinGW.

### Kernel include path
Kernel headers are at `third_party/minecraft-launcher-kernel/libmcbase/include/`. The CMakeLists adds this as an include directory; our code includes via `<mc_xxx.h>`.

---

## 5. Architecture Notes

### Signal/Slot chain (launch flow)
```
QML (LaunchPage) ──click──▶ KernelBridge::launchGame(memory)
                           ├── m_launchManager->launch()
                           │   └── doVerifyAndLaunch() → doLaunch()
                           │       └── QProcess (java.exe) started
                           └── [background thread] Java download if missing
                               └── JavaManager → JavaDownloadWorker
                                   └── emit javaDownloaded(path)
                                       └── onJavaReady() → doAuthAndLaunch()
```

### Minecraft-running detection
- `mc_mcPollThread` (QThread) runs every 15 s in background: `EnumWindows` → checks for Java process owning a "Minecraft" window → emits `minecraftRunningChanged`.
- `launchManager.running` tracks our-launched process state.
- `main.qml` computes `mcRunning = launchManager.running || kernel.minecraftRunning`.
- **Kill button disappears immediately** when `processFinished` signal fires (we added this in commit `195491d`).

### Settings persistence
- Launcher-wide: `<launcherDir>/settings.ini` (Qt QSettings INI format).
- Per-instance: `<launcherDir>/instances/<verId>/settings.ini`.
- Keys use dot notation: `java/memory`, `launch/jvmOptimize`, `language/index`, `ui/style`.
- `I18nManager` reads language from `language/index` at construction.
- **Language/style changes require restart** — handled by `restartDialog` popup (commit `195491d`).

### Memory allocation (auto mode)
Multi-tier budget allocator in `KernelBridge::computeAutoMemoryMB()`:
```
Tier 1: target 2 GB,  efficiency 1.00  (baseline)
Tier 2: target 6 GB,  efficiency 0.70  (modded play)
Tier 3: target 12 GB, efficiency 0.40  (heavy modpacks)
Algorithm: effective = budget × efficiency; allocate = min(incremental, effective)
           budget -= allocate / efficiency
Floor: 1024 MB. Result logged to launcher.log.
```
Called from `launchGame()` at launch time (not at settings-page open time).

---

## 6. Debugging & Diagnostics

### Slow event capture (main.cpp watchdog)
The `InstrumentedApplication::notify()` override and the separate watchdog thread log every event ≥ 50 ms:
```
[SlowEvent] 8414ms receiver=QThread name=modMirrorWarmup type=52
[SlowEvent] 195ms receiver=QThread name=modMirrorWarmup type=52
```
Event types: `type=52`=DeferredDelete, `type=77`=UpdateRequest, `type=50`=SockAct, `type=43`=MetaCall.
`[Stack]` prefix dumps the GUI thread stack (module+offset). Full-process stack dump is also available (`captureGuiStack` now enumerates all threads).

### CPU monitoring
Watchdog logs `proc cpu +Xms, gui thread cpu +Yms` on each stall. `GetProcessTimes`/`GetThreadTimes` used (NOT `clock()` — it reports wall-clock on Windows).

### Qt log files
- `dist/game/launcher.log` — our `mc_info`/`mc_error` output
- `dist/game/qtdebug.log` — Qt internal debug (set `QT_DEBUG_LEVEL=2` env var)
- `dist/game/mirrors.json` — mirror list (generated by pack.py)

### Recompile after kernel update
After replacing the kernel directory, always run a full rebuild (not just incremental):
```powershell
# Clear CMake cache if kernel ABI changed significantly
Remove-Item 'E:\launcher\launcher\build\CMakeCache.txt' -Force -ErrorAction SilentlyContinue
& cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -G Ninja
& cmake --build build --target Beacon --config Release --parallel 16
```

---

## 7. i18n Rules

- All user-visible strings go through `I18n.tr("key")` in QML.
- Keys are shared across all 4 languages (en, zh, fr, ja).
- **Never leave a key out of any language file** — fallback shows the raw key string (bugs visible in fr/ja `sourceAll` dropdown we fixed).
- Keys are dot-namespaced: `settings.memory`, `instance.jvmOptimize`, `modSearch.supportModrinth`.
- `%1`, `%2` placeholders work like Python `format()` — JSON values are plain strings, QML does `.arg()`.
- Add new keys to ALL four JSON files in the same commit. Validate with `ConvertFrom-Json` in PowerShell.

---

## 8. QML Conventions

- Use `I18n.tr("key")` for ALL text — never hardcode strings.
- `Theme` context property provides colors, shapes, dark mode flag.
- `kernel` context property gives access to `KernelBridge` singleton (`Q_INVOKABLE` methods).
- `Component.onCompleted` for one-time initialization; avoid heavy work there (causes first-frame stall).
- `Connections` target: `kernel` for bridge signals, `kernel.launchManager` for launch state.
- Custom dialogs: use `QtQuick.Dialogs` (`import QtQuick.Dialogs`). Our `restartDialog` uses `Dialog` + `standardButtons`.
- Do NOT use `Qt.quit()` in QML — the app manages its own lifecycle.

---

## 9. Windows-Specific Notes

- `dist/game/` is the persistent data dir (survives launcher updates); `dist/beacon/` is the app dir (replaced on update).
- `resolveLauncherDir()` returns `appDir/../game` on Windows. Never put version.txt or mirrors.json in the game dir — they belong beside the exe.
- `mc_http.cpp` TLS: new kernel uses raw pointer (no blocking teardown). Do NOT call `mc_http_release_thread_resources()` — it's gone from the header.
- Kill button: `Tint: "#ffffff"` always (primary-colour background in both themes).
- PowerShell in build scripts: no `&&` / `||` / `| head` — use `; if ($?)` and `Select-Object -First N`.

---

## 10. Commit Conventions

- Use conventional commits: `fix:`, `feat:`, `refactor:`, `chore:`
- Multi-line messages: `-m 'subject' -m 'body'` (wrap body in single quotes to avoid PowerShell parsing issues with `"`).
- Commits are local-only — do NOT push unless explicitly asked.
- LF→CRLF warnings are cosmetic; Git auto-converts on checkout (core.autocrlf=true in this repo).

---

## 11. Common Pitfalls

| Symptom | Cause | Fix |
|---------|-------|-----|
| `mc_http_release_thread_resources: not declared` | New kernel removed this API | Remove calls; TLS is now raw pointer |
| `mc_java_download_manifest_legacy: not declared` | Renamed to `mc_java_download_manifest` | Update call site |
| CMake generator mismatch ("Does not match") | Reconfigured with different generator | Delete `build/CMakeCache.txt` and re-run `pack.py` |
| QML `I18n.tr()` shows raw key | Missing translation in active language | Add key to that language's JSON |
| Auto-memory shows wrong value in preview | `applyAutoMemory()` and `launchGame()` must use same logic | Call `kernel.computeAutoMemoryMB()` from both |
| Kill button doesn't disappear after MC exits | Poller takes 15 s to catch process death | Connect to `launchManager.processFinished()` signal |
| `beacon.zip` stale in installer | `beacon.rc` embeds zip by mtime; `os.utime` needed | Call `os.utime(packaging/"beacon.rc")` after packing |

---

## 12. Files to Edit (Quick Reference)

| Task | File(s) |
|------|---------|
| New setting / UI text | `src/qml/pages/SettingsPage.qml` + 4× `src/qml/i18n/*.json` |
| JVM args / memory | `src/kernel/managers/LaunchManager.cpp` + `KernelBridge.cpp` |
| Instance-level settings | `src/qml/pages/InstanceSettingsPage.qml` |
| Kill button / tray | `src/qml/main.qml` + `src/cpp/KernelBridge.cpp` |
| Background worker threads | `src/cpp/main.cpp` (watchdog) + relevant manager |
| Kernel API change | `third_party/minecraft-launcher-kernel/` (replace dir) |
| Build/package | `pack.py`, `CMakeLists.txt` |

---

*Last updated: 2026-09-27. Kernel version: latest from https://github.com/fuqicn/minecraft-launcher-kernel.git*
