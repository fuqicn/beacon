#!/usr/bin/env python3
"""
Beacon cross-platform packaging script.

Auto-detects the host platform and builds + packages accordingly:

  Windows  -> dist/BeaconLauncher.exe (self-extracting C launcher embedding
              dist/beacon.zip) and dist/beacon.zip. Qt is deployed with
              windeployqt so the result runs on a clean machine.
  Linux    -> AppImage. A two-stage build produces a runnable payload AppImage
              (beacon-app.AppImage) plus a launcher AppImage (Beacon.AppImage)
              whose AppRun is a small C/GTK3 binary that installs/updates
              {beacon-app.AppImage, mirrors.json} into <its dir>/beacon and
              launches the payload through the runtime with a progress dialog
              (replicates the Windows launcher mechanism).

Stdlib only: zipfile, shutil, subprocess, platform, argparse, urllib, ...
Requires cmake/ninja + the platform toolchain (mingw on Windows, gcc/g++ +
linuxdeploy/appimagetool on Linux).
"""

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent

EXCLUDE_DIRS = (".qt", ".runtime", ".minecraft")
EXCLUDE_EXTS = (".ini", ".log")

LINUXDEPLOY_URL = (
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/"
    "linuxdeploy-x86_64.AppImage"
)
QT_PLUGIN_URL = (
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/"
    "linuxdeploy-plugin-qt-x86_64.AppImage"
)
APPIMAGETOOL_URL = (
    "https://github.com/AppImage/appimagetool/releases/download/continuous/"
    "appimagetool-x86_64.AppImage"
)
RUNTIME_URL = (
    "https://github.com/AppImage/type2-runtime/releases/download/continuous/"
    "runtime-x86_64"
)

# Prefix for GitHub release downloads to work around slow/blocked access.
# Set GH_PROXY to override, or pass --no-proxy to use GitHub directly.
DEFAULT_PROXY = "https://gh-proxy.com/"
DOWNLOAD_ATTEMPTS = 5


class PackError(Exception):
    pass


def log(msg):
    print(msg, flush=True)


def run(cmd, cwd=None, env=None, shell=False):
    log("+ " + (cmd if shell else subprocess.list2cmdline(cmd)))
    full_env = os.environ.copy()
    if env:
        full_env.update(env)
    subprocess.run(cmd, cwd=cwd, env=full_env, shell=shell, check=True)


def detect_platform():
    sysname = platform.system().lower()
    if sysname == "windows":
        return "windows"
    if sysname == "linux":
        return "linux"
    if sysname == "darwin":
        return "darwin"
    raise PackError("unsupported platform: %s" % sysname)


def read_cmake_cache(build_dir):
    cache = {}
    cache_file = Path(build_dir) / "CMakeCache.txt"
    if cache_file.is_file():
        for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"^([^:#=]+):([^=]*)=(.*)$", line)
            if m:
                cache[m.group(1)] = m.group(3)
    return cache


def resolve_version(args):
    if args.version:
        return args.version
    m = re.search(r"project\(\s*Beacon\s+VERSION\s+([^\s)]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    return m.group(1) if m else "1.0.0"


def _clean_cache_value(value):
    if not value or value.endswith("-NOTFOUND"):
        return None
    return value


def resolve_qt_dir(args, build_dir):
    if args.qt_dir:
        return Path(args.qt_dir)
    env = _clean_cache_value(os.environ.get("QT_DIR"))
    if env:
        return Path(env)
    q = _clean_cache_value(read_cmake_cache(build_dir).get("Qt6_DIR"))
    if q:
        p = Path(q)
        if p.name == "Qt6":
            return p.parent.parent.parent
        return p
    if detect_platform() == "linux":
        found = find_qt_on_linux()
        if found:
            return found
    return None


def find_qt_on_linux():
    for base in (Path.home() / "Qt", Path("/opt/Qt")):
        if not base.is_dir():
            continue
        candidates = []
        for ver_dir in base.iterdir():
            if not ver_dir.is_dir():
                continue
            gcc = ver_dir / "gcc_64"
            if (gcc / "lib" / "cmake" / "Qt6").is_dir():
                candidates.append(ver_dir)
        if not candidates:
            continue
        candidates.sort(key=lambda v: (tuple(int(x) for x in re.findall(r"\d+", v.name)),
                                       v.name))
        return candidates[-1] / "gcc_64"
    return None


def resolve_mingw_bin(args, build_dir):
    if args.mingw_bin:
        return Path(args.mingw_bin)
    cache = read_cmake_cache(build_dir)
    cc = _clean_cache_value(cache.get("CMAKE_CXX_COMPILER") or cache.get("CMAKE_C_COMPILER"))
    if cc:
        return Path(cc).parent
    return None


def build_type(args):
    return "Debug" if args.debug else "Release"


def resolve_cmake(build_dir):
    env = os.environ.get("CMAKE")
    if env:
        return Path(env)
    cache = read_cmake_cache(build_dir)
    cc = _clean_cache_value(cache.get("CMAKE_COMMAND"))
    if cc and Path(cc).is_file():
        return Path(cc)
    w = shutil.which("cmake")
    if w:
        return Path(w)
    raise PackError("cmake not found (set the CMAKE environment variable)")


def configure_and_build(args, build_dir, qt_dir):
    cmake = resolve_cmake(build_dir)
    build_dir = Path(build_dir)
    cfg = [str(cmake), "-B", str(build_dir), "-S", str(ROOT),
           "-DCMAKE_BUILD_TYPE=%s" % build_type(args)]
    if qt_dir:
        cfg.append("-DCMAKE_PREFIX_PATH=%s" % qt_dir)
    if detect_platform() == "linux":
        cfg.append("-DCMAKE_INSTALL_PREFIX=/usr")
    if not build_dir.exists():
        cfg.append("-G")
        cfg.append("Ninja")
        run(cfg)
    else:
        run(cfg)
    run([str(cmake), "--build", str(build_dir), "--target", "Beacon",
         "--parallel", str(args.jobs)])


def ensure_executable(path):
    path = Path(path)
    if not path.is_file():
        raise PackError("missing tool: %s" % path)
    os.chmod(path, os.stat(path).st_mode | 0o111)
    return path


def write_version_file(path, version):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("%s\n" % version, encoding="utf-8")


def sync_c_version(version):
    for name in ("main.c", "beacon_gtk.c"):
        src = ROOT / "packager" / name
        content = src.read_text(encoding="utf-8")
        updated = re.sub(r'#define BEACON_VERSION "[^"]*"',
                         '#define BEACON_VERSION "%s"' % version, content)
        if updated != content:
            src.write_text(updated, encoding="utf-8")
            log("packager/%s BEACON_VERSION -> %s" % (name, version))


def copy_mirrors_json(dst):
    candidates = [ROOT / "mirrors.json",
                  ROOT / "third_party" / "minecraft-launcher-kernel" / "mirrors.json"]
    for c in candidates:
        if c.is_file():
            shutil.copy2(c, dst)
            log("copied %s -> %s" % (c, dst))
            return
    log("WARNING: mirrors.json not found")


def zip_dir(src_dir, zip_path):
    src_dir = Path(src_dir)
    zip_path = Path(zip_path)
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()
    count = 0
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for base, dirs, files in os.walk(src_dir):
            base_rel = Path(base).relative_to(src_dir)
            dirs[:] = [d for d in dirs if not any(
                str(base_rel / d).replace("\\", "/") == x
                or str(base_rel / d).replace("\\", "/").startswith(x + "/")
                for x in EXCLUDE_DIRS)]
            for name in files:
                fpath = Path(base) / name
                rel = str((base_rel / name).as_posix())
                if any(rel == x or rel.startswith(x + "/") for x in EXCLUDE_DIRS):
                    continue
                if Path(name).suffix.lower() in EXCLUDE_EXTS:
                    log("SKIP %s" % rel)
                    continue
                zf.write(fpath, rel)
                count += 1
    log("zip: %s (%d files, %d bytes)" % (zip_path, count, zip_path.stat().st_size))


def download(url, dest, proxy):
    dest = Path(dest)
    if dest.is_file():
        log("already present: %s" % dest)
        return dest
    if proxy and url.startswith("https://github.com/"):
        url = proxy.rstrip("/") + "/" + url
    log("downloading %s" % url)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    last_err = None
    for attempt in range(1, DOWNLOAD_ATTEMPTS + 1):
        try:
            tmp.unlink(missing_ok=True)
            urllib.request.urlretrieve(url, tmp)
            tmp.replace(dest)
            return ensure_executable(dest)
        except Exception as e:
            last_err = e
            if attempt == DOWNLOAD_ATTEMPTS:
                break
            wait = min(2 ** (attempt - 1), 30)
            log("download failed (%s), retrying in %ds (%d/%d)"
                % (e, wait, attempt, DOWNLOAD_ATTEMPTS))
            time.sleep(wait)
    raise PackError("download failed after %d attempts: %s" % (DOWNLOAD_ATTEMPTS, last_err))


# ---------------------------------------------------------------- Windows ---

def build_windows(args, version, build_dir, qt_dir):
    log("=== Building Beacon for Windows ===")
    if not args.skip_build:
        configure_and_build(args, build_dir, qt_dir)

    dist = Path(args.dist_dir)
    beacon_dir = dist / "beacon"
    packager = ROOT / "packager"
    pack_tmp = packager / "pack-tmp"
    beacon_dir.mkdir(parents=True, exist_ok=True)

    exe = Path(build_dir) / "Beacon.exe"
    if not exe.is_file():
        raise PackError("Beacon.exe not found in %s" % exe)
    shutil.copy2(exe, beacon_dir / "Beacon.exe")
    log("copied Beacon.exe -> %s" % (beacon_dir / "Beacon.exe"))

    qt_root = qt_dir or resolve_qt_dir(args, build_dir)
    windeployqt = None
    if qt_root:
        for name in ("windeployqt.exe", "windeployqt"):
            cand = qt_root / "bin" / name
            if cand.is_file():
                windeployqt = cand
                break
    if windeployqt:
        log("--- Deploying Qt runtime ---")
        run([str(windeployqt), "--release", "--no-translations",
             "--no-opengl-sw", "--no-system-d3d-compiler",
             "--qmldir", str(ROOT / "src" / "qml"),
             str(beacon_dir / "Beacon.exe")])
    else:
        log("WARNING: windeployqt not found under %s; Qt runtime not deployed"
            % (qt_root or "unknown Qt dir"))

    copy_mirrors_json(beacon_dir / "mirrors.json")
    write_version_file(beacon_dir / "version.txt", version)

    log("--- Building beacon.zip ---")
    zip_path = pack_tmp / "beacon.zip"
    zip_dir(beacon_dir, zip_path)
    shutil.copy2(zip_path, packager / "beacon.zip")

    log("--- Building C launcher ---")
    sync_c_version(version)
    mingw_bin = resolve_mingw_bin(args, build_dir)
    windres = None
    gcc = None
    if mingw_bin:
        for name in ("windres.exe", "windres"):
            cand = mingw_bin / name
            if cand.is_file():
                windres = cand
                break
        for name in ("gcc.exe", "gcc"):
            cand = mingw_bin / name
            if cand.is_file():
                gcc = cand
                break
    if not windres or not gcc:
        raise PackError("windres/gcc not found (pass --mingw-bin or configure a mingw toolchain)")
    res = pack_tmp / "beacon.res"
    run([str(windres), "-O", "coff", str(packager / "beacon.rc"), "-o", str(res)])
    launcher = dist / "BeaconLauncher.exe"
    run([str(gcc), str(packager / "main.c"), str(res), "-o", str(launcher),
         "-lshell32", "-luser32", "-lgdi32", "-lcomctl32", "-O2", "-s", "-mwindows"])

    shutil.copy2(zip_path, dist / "beacon.zip")
    shutil.rmtree(pack_tmp, ignore_errors=True)

    log("=== Windows package done ===")
    log("  %s" % launcher)
    log("  %s" % (dist / "beacon.zip"))


# ---------------------------------------------------------------- Linux -----

def build_linux(args, version, build_dir, qt_dir):
    log("=== Building Beacon AppImages for Linux ===")
    if not args.skip_build:
        configure_and_build(args, build_dir, qt_dir)

    work = ROOT / args.work_dir
    work.mkdir(parents=True, exist_ok=True)
    tools_dir = Path(args.tools_dir) if args.tools_dir else work
    app_appdir = work / "app.AppDir"
    launch_appdir = work / "launch.AppDir"
    script_dir = ROOT / "appimage"

    os.environ["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    os.environ["ARCH"] = "x86_64"

    log("--- Installing into app AppDir ---")
    shutil.rmtree(app_appdir, ignore_errors=True)
    app_appdir.mkdir(parents=True)
    run(["cmake", "--install", str(build_dir)],
        env={"DESTDIR": str(app_appdir)})

    usr_share = app_appdir / "usr" / "share"
    (usr_share / "applications").mkdir(parents=True, exist_ok=True)
    (usr_share / "icons" / "hicolor" / "scalable" / "apps").mkdir(parents=True, exist_ok=True)
    shutil.copy2(script_dir / "beacon.desktop",
                 usr_share / "applications" / "io.github.fuqicn.beacon.desktop")
    shutil.copy2(ROOT / "Untitled.svg",
                 usr_share / "icons" / "hicolor" / "scalable" / "apps" / "io.github.fuqicn.beacon.svg")
    _symlink("usr/bin/Beacon", app_appdir / "Beacon")
    _symlink("usr/share/applications/io.github.fuqicn.beacon.desktop",
             app_appdir / "io.github.fuqicn.beacon.desktop")
    _symlink("usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg",
             app_appdir / "io.github.fuqicn.beacon.svg")
    copy_mirrors_json(app_appdir / "usr" / "bin" / "mirrors.json")
    write_version_file(app_appdir / "version.txt", version)

    if not qt_dir:
        qt_dir = resolve_qt_dir(args, build_dir)
    if not qt_dir:
        raise PackError("Qt dir not found (pass --qt-dir or set QT_DIR)")

    log("--- Downloading AppImage tools ---")
    linuxdeploy = download(LINUXDEPLOY_URL, tools_dir / "linuxdeploy", args.proxy)
    qt_plugin = download(QT_PLUGIN_URL, tools_dir / "linuxdeploy-plugin-qt", args.proxy)
    appimagetool = download(APPIMAGETOOL_URL, tools_dir / "appimagetool", args.proxy)

    runtime = None
    if args.runtime:
        runtime = Path(args.runtime)
        if not runtime.is_file():
            raise PackError("runtime file not found: %s" % runtime)
        log("using runtime file: %s" % runtime)
    else:
        runtime = download(RUNTIME_URL, tools_dir / "runtime", args.proxy)

    log("--- Running linuxdeploy (bundle shared libraries) ---")
    # Disable strip: linuxdeploy's bundled strip cannot handle Fedora 44's
    # .relr.dyn ELF sections; without stripping the AppImage still works fine.
    fake_strip = work / "fake_strip"
    fake_strip.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    os.chmod(fake_strip, 0o755)
    run([str(linuxdeploy), "--appdir", str(app_appdir)],
        cwd=work,
        env={"APPIMAGE_EXTRACT_AND_RUN": "1", "SKIP_UPDATEINFO": "1",
             "UPDATE_DESKTOP_DATABASE": "/bin/true",
             "gtk_update_icon_cache": "/bin/true",
             "STRIP": str(fake_strip)})

    log("--- Running linuxdeploy-plugin-qt (bundle Qt plugins/QML) ---")
    run([str(qt_plugin), "--appdir", str(app_appdir),
         "--exclude-library", "libqtiff.so",
         "--exclude-library", "libtiff.so*"],
        cwd=work,
        env={"QMAKE": str(qt_dir / "bin" / "qmake"), "APPIMAGE_EXTRACT_AND_RUN": "1",
             "SKIP_UPDATEINFO": "1", "UPDATE_DESKTOP_DATABASE": "/bin/true",
             "gtk_update_icon_cache": "/bin/true",
             "STRIP": str(fake_strip)})

    log("--- Installing AppRun ---")
    apprun = app_appdir / "AppRun"
    shutil.copy2(script_dir / "apprun.sh", apprun)
    os.chmod(apprun, 0o755)

    log("--- Building beacon-app.AppImage ---")
    payload_img = work / "beacon-app.AppImage"
    run([str(appimagetool), "--runtime-file", str(runtime),
         str(app_appdir), str(payload_img)],
        env={"VERSION": version, "APPIMAGE_EXTRACT_AND_RUN": "1"})
    os.chmod(payload_img, 0o755)

    log("--- Building GTK launcher ---")
    sync_c_version(version)
    gtk_launcher = work / "BeaconLauncher"
    pkg_cmd = ["pkg-config", "--cflags", "--libs", "gtk+-3.0", "gdk-x11-3.0", "x11"]
    pkg_flags = subprocess.check_output(pkg_cmd).decode().split()
    run(["gcc", "-O2", "-s", str(ROOT / "packager" / "beacon_gtk.c"),
         "-o", str(gtk_launcher)] + pkg_flags)

    log("--- Assembling launcher AppDir ---")
    shutil.rmtree(launch_appdir, ignore_errors=True)
    launch_appdir.mkdir(parents=True)
    shutil.copy2(payload_img, launch_appdir / "beacon-app.AppImage")
    write_version_file(launch_appdir / "version.txt", version)
    copy_mirrors_json(launch_appdir / "mirrors.json")
    desktop_text = (app_appdir / "usr" / "share" / "applications" /
                    "io.github.fuqicn.beacon.desktop").read_text(encoding="utf-8")
    desktop_text = re.sub(r"^Exec=Beacon$", "Exec=Beacon.AppImage",
                          desktop_text, flags=re.M)
    (launch_appdir / "io.github.fuqicn.beacon.desktop").write_text(desktop_text, encoding="utf-8")
    shutil.copy2(app_appdir / "usr" / "share" / "icons" / "hicolor" / "scalable" /
                 "apps" / "io.github.fuqicn.beacon.svg",
                 launch_appdir / "io.github.fuqicn.beacon.svg")

    log("--- Bundling GTK into launcher AppDir ---")
    run([str(linuxdeploy), "--appdir", str(launch_appdir)],
        cwd=work,
        env={"APPIMAGE_EXTRACT_AND_RUN": "1", "SKIP_UPDATEINFO": "1",
             "UPDATE_DESKTOP_DATABASE": "/bin/true",
             "gtk_update_icon_cache": "/bin/true",
             "STRIP": str(fake_strip)})

    # linuxdeploy generates its own AppRun; replace it with the C/GTK launcher
    # which self-bootstraps LD_LIBRARY_PATH from $APPDIR/usr/lib.
    gtk_apprun = launch_appdir / "AppRun"
    shutil.copy2(gtk_launcher, gtk_apprun)
    os.chmod(gtk_apprun, 0o755)

    log("--- Building Beacon.AppImage ---")
    launch_img = work / "Beacon.AppImage"
    run([str(appimagetool), "--runtime-file", str(runtime),
         str(launch_appdir), str(launch_img)],
        env={"VERSION": version, "APPIMAGE_EXTRACT_AND_RUN": "1"})
    os.chmod(launch_img, 0o755)

    dist = Path(args.dist_dir)
    dist.mkdir(parents=True, exist_ok=True)
    shutil.copy2(payload_img, dist / "beacon-app.AppImage")
    shutil.copy2(launch_img, dist / "Beacon.AppImage")

    log("=== Linux package done ===")
    log("  %s" % (dist / "Beacon.AppImage"))
    log("  %s" % (dist / "beacon-app.AppImage"))


def _symlink(target, link):
    if link.is_symlink() or link.exists():
        link.unlink()
    link.parent.mkdir(parents=True, exist_ok=True)
    os.symlink(target, link)


def build_macos(args, version):
    raise PackError("macOS packaging is not implemented yet")


# ----------------------------------------------------------------- main ----

def parse_args():
    p = argparse.ArgumentParser(
        description="Build and package Beacon for the current platform.")
    p.add_argument("--version", help="release version (default: from CMakeLists.txt)")
    p.add_argument("--debug", action="store_true", help="build Debug instead of Release")
    p.add_argument("--build-dir", help="CMake build directory (default: build / build-linux)")
    p.add_argument("--qt-dir", help="Qt root directory (override cache/env discovery)")
    p.add_argument("--mingw-bin", help="directory containing windres/gcc (Windows)")
    p.add_argument("--work-dir", default="build-appimage",
                   help="AppImage working directory (Linux, default: build-appimage)")
    p.add_argument("--dist-dir", default="dist", help="output directory (default: dist)")
    p.add_argument("--tools-dir", help="directory with cached AppImage tools (Linux)")
    p.add_argument("--runtime", help="AppImage runtime file (default: auto-download via proxy)")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                   help="parallel build jobs (default: cpu count)")
    p.add_argument("--skip-build", action="store_true",
                   help="skip configure/build (requires existing build artifacts)")
    p.add_argument("--proxy", default=os.environ.get("GH_PROXY") or DEFAULT_PROXY,
                   help="GitHub download proxy prefix (default: %s)" % DEFAULT_PROXY)
    p.add_argument("--no-proxy", action="store_true",
                   help="download GitHub releases directly, bypassing the proxy")
    return p.parse_args()


def main():
    args = parse_args()
    if args.no_proxy:
        args.proxy = ""
    version = resolve_version(args)
    plat = detect_platform()

    if plat == "windows":
        build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build"
        qt_dir = resolve_qt_dir(args, build_dir)
        build_windows(args, version, build_dir, qt_dir)
    elif plat == "linux":
        build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build-linux"
        qt_dir = resolve_qt_dir(args, build_dir)
        build_linux(args, version, build_dir, qt_dir)
    else:
        build_macos(args, version)


if __name__ == "__main__":
    try:
        main()
    except PackError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print("ERROR: command failed with exit code %d" % e.returncode, file=sys.stderr)
        sys.exit(e.returncode)