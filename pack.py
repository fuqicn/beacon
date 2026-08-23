#!/usr/bin/env python3
"""
Beacon cross-platform packaging script.

Auto-detects the host platform and builds + packages accordingly:

  Windows  -> dist/BeaconLauncher.exe (self-extracting C launcher embedding
              dist/beacon.zip) and dist/beacon.zip. Qt is deployed with
              windeployqt so the result runs on a clean machine.
  Linux    -> RPM/DEB package via rpmbuild/debhelper, or source tarball for
              openSUSE Build Service.

Stdlib only: zipfile, shutil, subprocess, platform, argparse, urllib, ...
Requires cmake/ninja + the platform toolchain (mingw on Windows, gcc/g++ on Linux).
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


# ---------------------------------------------------------------- Linux -----

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
    """Build and package for Linux (RPM/DEB)."""
    log("=== Building Beacon for Linux ===")
    if not args.skip_build:
        configure_and_build(args, build_dir, qt_dir)

    work = ROOT / args.work_dir
    work.mkdir(parents=True, exist_ok=True)
    dist = Path(args.dist_dir)
    dist.mkdir(parents=True, exist_ok=True)

    # Build source tarball for openSUSE Build Service
    src_tarball = dist / "beacon-%s.tar.gz" % version
    run(["tar", "czf", str(src_tarball), "--exclude=.git", "--exclude=build*", "."],
        cwd=str(ROOT))
    log("Source tarball: %s" % src_tarball)

    # Build RPM if rpmbuild is available
    if shutil.which("rpmbuild"):
        _build_rpm(args, version, build_dir, dist)

    # Build DEB if dpkg-deb is available
    if shutil.which("dpkg-deb"):
        _build_deb(args, version, build_dir, dist)

    log("=== Linux build done ===")


def _build_rpm(args, version, build_dir, dist):
    """Build RPM package using rpmbuild."""
    log("--- Building RPM ---")
    rpm_buildroot = Path(args.work_dir) / "rpm-build"
    rpm_buildroot.mkdir(parents=True, exist_ok=True)

    # Prepare source directory
    src_dir = rpm_buildroot / "SOURCES"
    src_dir.mkdir(parents=True, exist_ok=True)
    spec_dir = rpm_buildroot / "SPECS"
    spec_dir.mkdir(parents=True, exist_ok=True)

    # Copy source tarball
    src_tarball = dist / "beacon-%s.tar.gz" % version
    shutil.copy2(src_tarball, src_dir / "beacon-%s.tar.gz" % version)

    # Generate spec file
    spec_content = _generate_spec(version)
    spec_file = spec_dir / "beacon.spec"
    spec_file.write_text(spec_content, encoding="utf-8")

    # Build RPM
    env = os.environ.copy()
    env["HOME"] = str(rpm_buildroot)
    run(["rpmbuild", "-bb", "--define", "_topdir %s" % rpm_buildroot,
         str(spec_file)], cwd=str(rpm_buildroot), env=env)

    # Copy RPM to dist
    rpm_dir = rpm_buildroot / "RPMS" / "noarch"
    for rpm_file in rpm_dir.glob("*.rpm"):
        shutil.copy2(rpm_file, dist)
    log("RPM package: %s" % list(dist.glob("*.rpm")))


def _build_deb(args, version, build_dir, dist):
    """Build DEB package using debhelper."""
    log("--- Building DEB ---")
    deb_buildroot = Path(args.work_dir) / "deb-build"
    deb_buildroot.mkdir(parents=True, exist_ok=True)

    # Create debian package structure
    pkg_dir = deb_buildroot / "beacon-%s" % version
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)
    pkg_dir.mkdir(parents=True)

    # Install files
    install_dir = pkg_dir / "usr"
    install_dir.mkdir(parents=True)
    bin_dir = install_dir / "bin"
    bin_dir.mkdir(parents=True)
    share_dir = install_dir / "share"
    share_dir.mkdir(parents=True)
    appdir = share_dir / "applications"
    appdir.mkdir(parents=True)
    icons_dir = share_dir / "icons" / "hicolor" / "scalable" / "apps"
    icons_dir.mkdir(parents=True, parents=True)
    beacon_dir = share_dir / "beacon"
    beacon_dir.mkdir(parents=True)

    # Copy binary
    binary = Path(build_dir) / "Beacon"
    if binary.exists():
        shutil.copy2(binary, bin_dir / "Beacon")
        os.chmod(bin_dir / "Beacon", 0o755)

    # Copy resources
    mirrors_src = ROOT / "third_party" / "minecraft-launcher-kernel" / "mirrors.json"
    if mirrors_src.exists():
        shutil.copy2(mirrors_src, beacon_dir / "mirrors.json")
    svg_src = ROOT / "Untitled.svg"
    if svg_src.exists():
        shutil.copy2(svg_src, icons_dir / "io.github.fuqicn.beacon.svg")

    # Create desktop file
    desktop = appdir / "io.github.fuqicn.beacon.desktop"
    desktop.write_text("""[Desktop Entry]
Name=Beacon
Exec=Beacon
Icon=io.github.fuqicn.beacon
Type=Application
Categories=Game;
""", encoding="utf-8")

    # Create debian control file
    debian_dir = pkg_dir / "debian"
    debian_dir.mkdir(parents=True)
    control = debian_dir / "control"
    control.write_text("""Package: beacon
Version: %s
Section: games
Priority: optional
Architecture: all
Maintainer: fuqicn <fuqi2012cn@outlook.com>
Description: Cross-platform Minecraft launcher
 Beacon is a cross-platform Minecraft launcher.
""" % version, encoding="utf-8")

    # Build DEB
    run(["dpkg-deb", "--build", str(pkg_dir), str(dist / "beacon_%s_all.deb" % version)])
    log("DEB package: %s" % (dist / "beacon_%s_all.deb" % version))


def _generate_spec(version):
    """Generate RPM spec file content."""
    return """Name:           beacon
Version:        %s
Release:        0
Summary:        Cross-platform Minecraft launcher
License:        GPL-3.0-or-later
URL:            https://github.com/fuqicn/beacon
Source0:        %%{name}-%%{version}.tar.gz
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtquickcontrols2-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  zlib-devel
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtquickcontrols2
Requires:       qt6-qtsvg
Requires:       zlib

%%description
Beacon is a cross-platform Minecraft launcher with support for mods, modpacks,
and multiple instances.

%%prep
%%setup -q

%%build
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel %%(cpu_count)

%%install
cmake --install build --prefix %%(buildroot)/usr

%%files
/usr/bin/Beacon
/usr/share/applications/io.github.fuqicn.beacon.desktop
/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg
/usr/share/beacon/mirrors.json

%%changelog
* Mon Aug 25 2025 fuqicn <fuqi2012cn@outlook.com> - %s-0
- Initial package for openSUSE Build Service
""" % (version, version)


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
    p.add_argument("--work-dir", default="build-linux",
                   help="Linux build working directory (default: build-linux)")
    p.add_argument("--dist-dir", default="dist", help="output directory (default: dist)")
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