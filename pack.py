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


def bundle_dependencies(appdir_path):
    """Copy all shared library dependencies into the AppDir.

    Uses ldd to find dependencies, copies them to usr/lib, and sets rpath
    so the binary can find them at runtime.
    """
    import shutil as sh
    import subprocess as sp

    binary = appdir_path / "usr" / "bin" / "Beacon"
    if not binary.exists():
        log("WARNING: Beacon binary not found, skipping dependency bundling")
        return

    log("bundling dependencies for %s" % binary)

    # Use ldd to find all shared library dependencies
    try:
        result = sp.run(["ldd", str(binary)], capture_output=True, text=True)
        if result.returncode != 0:
            log("WARNING: ldd failed: %s" % result.stderr[:200])
            return
    except Exception as e:
        log("WARNING: ldd execution failed: %s" % e)
        return

    lib_dir = appdir_path / "usr" / "lib"
    lib_dir.mkdir(parents=True, exist_ok=True)

    copied = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        # Parse ldd output
        lib_path = None
        parts = line.split()
        if len(parts) >= 3 and parts[1] == "=>":
            lib_path = parts[2]
        elif len(parts) >= 2 and parts[0].endswith(".so*"):
            lib_path = parts[0]
        if not lib_path or lib_path == "(null)":
            continue

        src = Path(lib_path)
        if not src.exists():
            continue

        # Resolve symlinks to get the real file
        try:
            real_src = src.resolve()
        except Exception:
            real_src = src

        # Copy the library and all symlinks in the chain
        dst = lib_dir / real_src.name
        if not dst.exists():
            try:
                sh.copy2(real_src, dst)
                copied.append(real_src.name)
            except Exception as e:
                log("WARNING: failed to copy %s: %s" % (real_src.name, e))

        # Also copy any symlinks pointing to this library
        if real_src.is_symlink() or src.is_symlink():
            try:
                link_target = os.readlink(real_src if real_src.is_symlink() else src)
                symlink_name = real_src.name if real_src.is_symlink() else src.name
                dst_symlink = lib_dir / symlink_name
                if not dst_symlink.exists():
                    os.symlink(link_target, dst_symlink)
            except Exception:
                pass

    log("copied %d libraries" % len(copied))

    # Set rpath using patchelf
    patchelf = sh.which("patchelf")
    if patchelf:
        try:
            sp.run([patchelf, "--set-rpath", "$ORIGIN/../lib", str(binary)],
                   capture_output=True, check=True)
            log("set rpath to $ORIGIN/../lib")
        except Exception as e:
            log("WARNING: patchelf failed: %s" % e)
    else:
        log("WARNING: patchelf not found")


def patch_linuxdeploy_strip(tools_dir):
    """Replace linuxdeploy's bundled strip with system strip.

    linuxdeploy ships an old strip that cannot handle Fedora 44's .relr.dyn
    ELF section. Since linuxdeploy ignores the STRIP environment variable,
    we must extract the AppImage, replace its embedded strip, and run from
    the extracted directory.
    """
    import shutil as sh

    appimage = tools_dir / "linuxdeploy"
    if not appimage.is_file():
        return str(appimage)

    system_strip = sh.which("strip")
    if not system_strip:
        log("WARNING: strip not found in PATH, skipping linuxdeploy patch")
        return str(appimage)

    # Check if we need to patch (system strip might already be new enough)
    # We'll always patch to be safe since we can't easily check .relr.dyn support

    extract_dir = tools_dir / ".linuxdeploy_extract"
    squashfs_root = extract_dir / "squashfs-root"

    if not squashfs_root.exists():
        log("extracting linuxdeploy for strip patching...")
        # Try multiple extraction methods
        extracted = False

        # Method 1: Try unsquashfs (fast, reliable if available)
        import subprocess as sp
        unsquashfs = sh.which("unsquashfs")
        if unsquashfs:
            try:
                # Create a temp dir for extraction
                tmpdir = extract_dir.parent / ".linuxdeploy_tmp"
                tmpdir.mkdir(parents=True, exist_ok=True)
                result = sp.run([unsquashfs, "-d", str(squashfs_root),
                                 str(appimage)], capture_output=True, text=True)
                if result.returncode == 0 and squashfs_root.exists():
                    extracted = True
                    log("extracted using unsquashfs")
            except Exception as e:
                log("unsquashfs failed: %s" % e)

        # Method 2: Try running the AppImage with --appimage-extract
        if not extracted:
            try:
                os.chmod(appimage, 0o755)
                env = os.environ.copy()
                env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
                env["TMPDIR"] = str(extract_dir.parent)
                # AppImage runtime may extract to a known location
                result = sp.run(["./" + str(appimage), "--appimage-extract"],
                                cwd=str(tools_dir), capture_output=True, text=True,
                                env=env)
                if result.returncode == 0 and squashfs_root.exists():
                    extracted = True
                    log("extracted using AppImage runtime")
                else:
                    log("AppImage extraction failed: %s" % result.stderr[:200])
            except Exception as e:
                log("AppImage runtime extraction failed: %s" % e)

        # Method 3: Try Python's zipfile (for zip-based AppImages)
        if not extracted:
            try:
                import zipfile
                tmpdir = extract_dir.parent / ".linuxdeploy_zip_tmp"
                tmpdir.mkdir(parents=True, exist_ok=True)
                with zipfile.ZipFile(appimage, 'r') as zf:
                    zf.extractall(tmpdir)
                # Find squashfs-root
                for root, dirs, files in os.walk(tmpdir):
                    if "squashfs-root" in dirs:
                        sq_root = Path(root) / "squashfs-root"
                        sq_root.rename(squashfs_root)
                        extracted = True
                        log("extracted using zipfile")
                        break
                if not extracted:
                    # Try moving the whole content
                    items = [p for p in tmpdir.iterdir() if p.name != '.extracted']
                    if items:
                        squashfs_root.parent.mkdir(parents=True, exist_ok=True)
                        for item in items:
                            item.rename(squashfs_root / item.name)
                        extracted = True
                        log("extracted using zipfile (flat)")
            except Exception as e:
                log("zipfile extraction failed: %s" % e)

        if not extracted:
            log("WARNING: all extraction methods failed, using original linuxdeploy")
            return str(appimage)

    # Replace embedded strip with system strip
    src_strip = squashfs_root / "usr" / "bin" / "strip"
    if src_strip.exists():
        log("replacing embedded strip with system strip: %s" % system_strip)
        sh.copy2(system_strip, src_strip)
        os.chmod(src_strip, 0o755)
        log("patched: %s -> %s" % (src_strip, system_strip))
    else:
        log("WARNING: strip not found at %s" % src_strip)
        # List contents for debugging
        usr_bin = squashfs_root / "usr" / "bin"
        if usr_bin.exists():
            log("contents of usr/bin: %s" % [f.name for f in usr_bin.iterdir()])

    # Return path to extracted linuxdeploy binary
    patched_linuxdeploy = squashfs_root / "usr" / "bin" / "linuxdeploy"
    if patched_linuxdeploy.exists():
        log("using patched linuxdeploy from: %s" % patched_linuxdeploy)
        return str(patched_linuxdeploy)

    log("WARNING: linuxdeploy binary not found at expected path")
    return str(appimage)


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

    # Copy mirrors.json
    copy_mirrors_json(app_appdir / "usr" / "bin" / "mirrors.json")

    # Create simple AppRun that just runs the binary
    apprun = app_appdir / "AppRun"
    apprun.write_text("""#!/bin/sh
# Beacon self-extracting AppImage AppRun
# Static binary - no runtime dependencies needed
exec "$(dirname "$0")/usr/bin/Beacon" "$@"
""", encoding="utf-8")
    os.chmod(apprun, 0o755)

    # Create desktop file (must be in AppDir root for appimagetool)
    usr_share = app_appdir / "usr" / "share"
    (usr_share / "applications").mkdir(parents=True, exist_ok=True)
    (usr_share / "icons" / "hicolor" / "scalable" / "apps").mkdir(parents=True, exist_ok=True)
    desktop = usr_share / "applications" / "io.github.fuqicn.beacon.desktop"
    desktop.write_text("""[Desktop Entry]
Name=Beacon
Exec=Beacon
Icon=io.github.fuqicn.beacon
Type=Application
Categories=Game;
""", encoding="utf-8")
    # Also copy to root for appimagetool
    (app_appdir / "Beacon.desktop").write_text("""[Desktop Entry]
Name=Beacon
Exec=Beacon
Icon=io.github.fuqicn.beacon
Type=Application
Categories=Game;
""", encoding="utf-8")
    shutil.copy2(ROOT / "Untitled.svg",
                 app_appdir / "io.github.fuqicn.beacon.svg")
    shutil.copy2(ROOT / "Untitled.svg",
                 usr_share / "icons" / "hicolor" / "scalable" / "apps" / "io.github.fuqicn.beacon.svg")

    log("--- Downloading AppImage tools ---")
    appimagetool = download(APPIMAGETOOL_URL, tools_dir / "appimagetool", args.proxy)

    log("--- Building beacon-app.AppImage ---")
    payload_img = work / "beacon-app.AppImage"
    # Move to ASCII-only temp dir to avoid appimagetool encoding issues with Chinese paths
    tmp_work = work / ".appimage_tmp"
    tmp_work.mkdir(exist_ok=True)
    tmp_appdir = tmp_work / "appdir"
    tmp_payload = tmp_work / "beacon-app.AppImage"
    shutil.copytree(app_appdir, tmp_appdir, dirs_exist_ok=True)
    # Bundle all shared library dependencies
    bundle_dependencies(tmp_appdir)
    # Download runtime if needed
    runtime = None
    if args.runtime:
        runtime = Path(args.runtime)
        if not runtime.is_file():
            raise PackError("runtime file not found: %s" % runtime)
        log("using runtime file: %s" % runtime)
    else:
        runtime = download(RUNTIME_URL, tools_dir / "runtime", args.proxy)
    run([str(appimagetool), "--runtime-file", str(runtime),
         str(tmp_appdir), str(tmp_payload)],
        env={"VERSION": version, "APPIMAGE_EXTRACT_AND_RUN": "1", "TMPDIR": str(tmp_work)})
    tmp_payload.rename(payload_img)
    os.chmod(payload_img, 0o755)

    dist = Path(args.dist_dir)
    dist.mkdir(parents=True, exist_ok=True)
    shutil.copy2(payload_img, dist / "Beacon.AppImage")

    log("=== Linux package done ===")
    log("  %s" % (dist / "Beacon.AppImage"))


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