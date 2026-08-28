#!/usr/bin/env python3
"""
Beacon cross-platform packaging script.

Auto-detects the host platform and builds + packages accordingly:

  Windows  -> dist/BeaconLauncher-windows-<arch>.exe (self-extracting C launcher
              embedding dist/beacon.zip) and dist/beacon.zip. Qt is deployed with
              windeployqt so the result runs on a clean machine.
  Linux    -> native packages built from a `cmake --install` staging tree:
                dist/BeaconLauncher-deb-<deb_arch>.deb          (dpkg-deb)
                dist/BeaconLauncher-redhat-<rpm_arch>.rpm       (rpmbuild + .spec)
                dist/BeaconLauncher-arch-<pacman_arch>.pkg.tar.zst (makepkg + PKGBUILD)
              Missing tooling for one format only skips that format; if no
              tooling exists at all, a plain tar.gz fallback is produced.

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
    return m.group(1) if m else "1.0.1"


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
    # Check Qt's default toolchain locations
    for candidate in [
        Path("E:/Qt/Tools/CMake_64/bin/cmake.exe"),
        Path("E:/Qt/Tools/CMake_64/bin/cmake"),
    ]:
        if candidate.is_file():
            return candidate
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
        cfg.append("-DLINUX_NO_CACHEGEN=ON")
    if not build_dir.exists():
        cfg.append("-G")
        cfg.append("Ninja")
        # Resolve Ninja path
        ninja_bin = shutil.which("ninja")
        if not ninja_bin:
            for candidate in [
                Path(qt_dir).parent / "Tools" / "Ninja" / "ninja.exe" if qt_dir else None,
                Path("E:/Qt/Tools/Ninja/ninja.exe"),
            ]:
                if candidate and candidate.is_file():
                    ninja_bin = str(candidate)
                    break
        if ninja_bin:
            cfg.append("-DCMAKE_MAKE_PROGRAM=%s" % ninja_bin)
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
        src = ROOT / "packaging" / name
        content = src.read_text(encoding="utf-8")
        updated = re.sub(r'#define BEACON_VERSION "[^"]*"',
                         '#define BEACON_VERSION "%s"' % version, content)
        if updated != content:
            src.write_text(updated, encoding="utf-8")
            log("packaging/%s BEACON_VERSION -> %s" % (name, version))


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
    packaging = ROOT / "packaging"
    pack_tmp = packaging / "pack-tmp"
    beacon_dir.mkdir(parents=True, exist_ok=True)

    exe = Path(build_dir) / "Beacon.exe"
    if not exe.is_file():
        raise PackError("Beacon.exe not found in %s" % exe)
    shutil.copy2(exe, beacon_dir / "Beacon.exe")
    log("copied Beacon.exe -> %s" % (beacon_dir / "Beacon.exe"))

    # Toolchain dir shared by strip/windres/gcc below.
    mingw_bin = resolve_mingw_bin(args, build_dir)

    # Strip the deployed binary to cut package size. The unstripped original
    # stays in build/ so crash.log symbolization keeps working in dev builds.
    if mingw_bin:
        strip = None
        for name in ("strip.exe", "strip"):
            cand = mingw_bin / name
            if cand.is_file():
                strip = cand
                break
        if strip:
            deployed = beacon_dir / "Beacon.exe"
            before = deployed.stat().st_size
            run([str(strip), "--strip-all", str(deployed)])
            after = deployed.stat().st_size
            log("stripped Beacon.exe: %d -> %d bytes (-%.0f%%)"
                % (before, after, 100.0 * (before - after) / max(before, 1)))
        else:
            log("WARNING: strip not found in %s; Beacon.exe not stripped" % mingw_bin)

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
    shutil.copy2(zip_path, packaging / "beacon.zip")

    log("--- Building C launcher ---")
    sync_c_version(version)
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
    run([str(windres), "-O", "coff", str(packaging / "beacon.rc"), "-o", str(res)])
    arch_tag = "amd64" if platform.machine().lower() in ("x86_64", "amd64") else platform.machine().lower()
    launcher = dist / ("BeaconLauncher-windows-%s.exe" % arch_tag)
    run([str(gcc), str(packaging / "main.c"), str(res), "-o", str(launcher),
         "-lshell32", "-luser32", "-lgdi32", "-lcomctl32", "-O2", "-s", "-mwindows"])

    shutil.copy2(zip_path, dist / "beacon.zip")
    shutil.rmtree(pack_tmp, ignore_errors=True)

    log("=== Windows package done ===")
    log("  %s" % launcher)
    log("  %s" % (dist / "beacon.zip"))


# ---------------------------------------------------------------- Linux -----

MAINTAINER = "fuqicn <fuqi2012cn@outlook.com>"
HOMEPAGE = "https://github.com/fuqicn/beacon"
APP_ID = "io.github.fuqicn.beacon"
BIN_NAME = "Beacon"

DESKTOP_ENTRY = """[Desktop Entry]
Type=Application
Name=Beacon
Comment=A cross-platform Minecraft launcher
Exec=%(bin)s
TryExec=%(bin)s
Icon=%(app_id)s
Terminal=false
Categories=Game;ActionGame;
Keywords=minecraft;launcher;beacon;
"""

DEB_DEPS = ("libqt6core6t64 | libqt6core6, libqt6gui6, libqt6network6, "
            "libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, "
            "libqt6svg6, libqt6widgets6")

RPM_SPEC_TEMPLATE = """Name:           beacon-launcher
Version:        @VERSION@
Release:        1%{?dist}
Summary:        A cross-platform Minecraft launcher

License:        GPL-3.0-or-later
URL:            @HOMEPAGE@
BuildArch:      @ARCH@

Requires:       qt6-qtbase >= 6.4
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg
Requires:       zlib

%description
Beacon is a Qt 6 based Minecraft launcher featuring Microsoft and offline
login, version/mod/modpack downloads and per-instance management.

%prep
%build

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
mkdir -p %{buildroot}%{_datadir}/beacon
install -m 0755 '@STAGING@/usr/bin/@BIN@'      %{buildroot}%{_bindir}/@BIN@
install -m 0644 '@STAGING@/usr/share/applications/@APPID@.desktop'  %{buildroot}%{_datadir}/applications/@APPID@.desktop
install -m 0644 '@STAGING@/usr/share/icons/hicolor/scalable/apps/@APPID@.svg' %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/@APPID@.svg
install -m 0644 '@STAGING@/usr/share/beacon/mirrors.json'  %{buildroot}%{_datadir}/beacon/mirrors.json
install -m 0644 '@STAGING@/usr/share/beacon/version.txt'   %{buildroot}%{_datadir}/beacon/version.txt

%files
%{_bindir}/@BIN@
%{_datadir}/applications/@APPID@.desktop
%{_datadir}/icons/hicolor/scalable/apps/@APPID@.svg
%{_datadir}/beacon/mirrors.json
%{_datadir}/beacon/version.txt

%changelog
* @RPM_DATE@ fuqicn <fuqi2012cn@outlook.com> - @VERSION@-1
- Upstream release @VERSION@
"""

PKGBUILD_TEMPLATE = """# Maintainer: fuqicn <fuqi2012cn@outlook.com>
pkgname=beacon-launcher
pkgver=@VERSION@
pkgrel=1
pkgdesc='A cross-platform Minecraft launcher'
arch=('@ARCH@')
url='@HOMEPAGE@'
license=('GPL3')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg')

package() {
    install -Dm755 '@STAGING@/usr/bin/@BIN@' "${pkgdir}/usr/bin/@BIN@"
    install -Dm644 '@STAGING@/usr/share/applications/@APPID@.desktop' "${pkgdir}/usr/share/applications/@APPID@.desktop"
    install -Dm644 '@STAGING@/usr/share/icons/hicolor/scalable/apps/@APPID@.svg' "${pkgdir}/usr/share/icons/hicolor/scalable/apps/@APPID@.svg"
    install -Dm644 '@STAGING@/usr/share/beacon/mirrors.json' "${pkgdir}/usr/share/beacon/mirrors.json"
    install -Dm644 '@STAGING@/usr/share/beacon/version.txt' "${pkgdir}/usr/share/beacon/version.txt"
}
"""


def map_arch():
    """Map host machine to (deb_arch, rpm_arch, pacman_arch)."""
    m = platform.machine().lower()
    if m in ("x86_64", "amd64"):
        return {"deb": "amd64", "rpm": "x86_64", "pacman": "x86_64"}
    if m in ("aarch64", "arm64"):
        return {"deb": "arm64", "rpm": "aarch64", "pacman": "aarch64"}
    raise PackError("unsupported architecture: %s" % m)


def detect_linux_family():
    """Best-effort detection of the host distro family."""
    if shutil.which("dpkg"):
        return "debian"
    if shutil.which("rpmbuild") or shutil.which("rpm"):
        return "fedora"
    if shutil.which("makepkg"):
        return "arch"
    osr = Path("/etc/os-release")
    if osr.is_file():
        text = osr.read_text(encoding="utf-8", errors="replace").lower()
        ids = []
        for line in text.splitlines():
            m = re.match(r"^id(?:_like)?=(.*)$", line.strip())
            if m:
                ids += [x.strip().strip('"') for x in m.group(1).split()]
        if any(i in ids for i in ("debian", "ubuntu")):
            return "debian"
        if any(i in ids for i in ("fedora", "rhel", "centos", "rocky", "alma")):
            return "fedora"
        if any(i in ids for i in ("arch", "manjaro")):
            return "arch"
    return None


def stage_install_tree(args, version, build_dir):
    """Install the build tree into a staging prefix and add desktop metadata."""
    cmake = resolve_cmake(build_dir)
    staging = Path(args.work_dir) / "linux-staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    run([str(cmake), "--install", str(build_dir), "--prefix",
         str((staging / "usr").resolve())])

    # Strip the staged binary when a toolchain strip is available (OBS builds
    # get distro-managed stripping; this covers the plain tarball fallback).
    strip = shutil.which("strip")
    staged_bin = staging / "usr" / "bin" / BIN_NAME
    if strip and staged_bin.is_file():
        before = staged_bin.stat().st_size
        run([strip, "--strip-all", str(staged_bin)])
        log("stripped %s: %d -> %d bytes"
            % (staged_bin, before, staged_bin.stat().st_size))

    # Desktop entry (Exec uses the packaged binary name only)
    apps = staging / "usr/share/applications"
    apps.mkdir(parents=True, exist_ok=True)
    (apps / ("%s.desktop" % APP_ID)).write_text(
        DESKTOP_ENTRY % {"bin": BIN_NAME, "app_id": APP_ID}, encoding="utf-8")

    # Runtime version file consumed by the in-app updater on Linux
    ver_dir = staging / "usr/share/beacon"
    ver_dir.mkdir(parents=True, exist_ok=True)
    write_version_file(ver_dir / "version.txt", version)
    return staging


def build_deb(staging, version, dist):
    """Build the Debian package via dpkg-deb."""
    if not shutil.which("dpkg-deb"):
        log("WARNING: dpkg-deb not found; skipping .deb package")
        return None
    arch = map_arch()["deb"]
    root = staging.parent / "deb-pkg"
    if root.exists():
        shutil.rmtree(root)
    shutil.copytree(staging, root)          # root/usr/...

    size_kb = sum(f.stat().st_size for f in root.rglob("*") if f.is_file()) // 1024
    debian = root / "DEBIAN"
    debian.mkdir()
    control = (
        "Package: beacon-launcher\n"
        "Version: %s\n"
        "Section: games\n"
        "Priority: optional\n"
        "Architecture: %s\n"
        "Installed-Size: %d\n"
        "Maintainer: %s\n"
        "Depends: %s\n"
        "Homepage: %s\n"
        "Description: A cross-platform Minecraft launcher\n"
        " Beacon is a Qt 6 based Minecraft launcher featuring Microsoft and\n"
        " offline login, version/mod/modpack downloads and per-instance\n"
        " management.\n" % (version, arch, size_kb, MAINTAINER, DEB_DEPS, HOMEPAGE))
    (debian / "control").write_text(control, encoding="utf-8")

    out = Path(dist) / ("BeaconLauncher-deb-%s.deb" % arch)
    out.unlink(missing_ok=True)
    run(["dpkg-deb", "--build", "--root-owner-group", str(root.resolve()), str(out)])
    return out


def build_rpm(staging, version, dist):
    """Build the RPM package via rpmbuild."""
    rpmbuild = shutil.which("rpmbuild")
    if not rpmbuild:
        log("WARNING: rpmbuild not found; skipping .rpm package")
        return None
    arch = map_arch()["rpm"]
    top = staging.parent / "rpm-top"
    if top.exists():
        shutil.rmtree(top)
    for sub in ("BUILD", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (top / sub).mkdir(parents=True)

    spec_path = top / "SPECS" / "beacon-launcher.spec"
    rpm_date = time.strftime("%a %b %d %Y")
    spec_path.write_text(
        RPM_SPEC_TEMPLATE
        .replace("@VERSION@", version)
        .replace("@ARCH@", arch)
        .replace("@HOMEPAGE@", HOMEPAGE)
        .replace("@STAGING@", str(Path(staging).resolve()))
        .replace("@BIN@", BIN_NAME)
        .replace("@APPID@", APP_ID)
        .replace("@RPM_DATE@", rpm_date),
        encoding="utf-8")

    run([rpmbuild, "-bb", "--define", "_topdir %s" % top.resolve(),
         "--target", "%s-pc-linux-gnu" % arch, str(spec_path)])
    produced = list((top / "RPMS").rglob("*.rpm"))
    if not produced:
        raise PackError("rpmbuild produced no package")
    out = Path(dist) / ("BeaconLauncher-redhat-%s.rpm" % arch)
    out.unlink(missing_ok=True)
    shutil.copy2(produced[0], out)
    return out


def build_arch_pkg(staging, version, dist):
    """Build the Arch package via makepkg."""
    if not shutil.which("makepkg"):
        log("WARNING: makepkg not found; skipping Arch package")
        return None
    arch = map_arch()["pacman"]
    work = staging.parent / "arch-pkg"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    cmd = ["makepkg", "-f", "-d"]     # -f force overwrite, -d skip dep check
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        cmd.append("--asroot")         # CI containers often run as root
    (work / "PKGBUILD").write_text(
        PKGBUILD_TEMPLATE
        .replace("@VERSION@", version)
        .replace("@ARCH@", arch)
        .replace("@HOMEPAGE@", HOMEPAGE)
        .replace("@STAGING@", str(Path(staging).resolve()))
        .replace("@BIN@", BIN_NAME)
        .replace("@APPID@", APP_ID),
        encoding="utf-8")
    run(cmd, cwd=work)

    produced = sorted(work.glob("*.pkg.tar.*"))
    if not produced:
        raise PackError("makepkg produced no package")
    out = Path(dist) / ("BeaconLauncher-arch-%s.pkg.tar.zst" % arch)
    out.unlink(missing_ok=True)
    shutil.copy2(produced[-1], out)
    return out


def build_linux(args, version, build_dir, qt_dir):
    """Build and package native packages for Debian/Fedora/Arch."""
    log("=== Building Beacon for Linux ===")
    if not args.skip_build:
        configure_and_build(args, build_dir, qt_dir)

    binary = Path(build_dir) / BIN_NAME
    if not binary.is_file():
        raise PackError("Binary not found: %s" % binary)

    dist = Path(args.dist_dir)
    dist.mkdir(parents=True, exist_ok=True)

    family = detect_linux_family()
    log("detected distro family: %s" % (family or "unknown"))

    staging = stage_install_tree(args, version, build_dir)

    made = [p for p in (build_deb(staging, version, dist),
                        build_rpm(staging, version, dist),
                        build_arch_pkg(staging, version, dist)) if p]

    if not made:
        # No packaging tooling available — fall back to an installable tarball.
        tarball = dist / ("beacon-%s-linux-%s.tar.gz"
                          % (version, map_arch()["pacman"]))
        run(["tar", "-czf", str(tarball), "-C", str(staging), "usr"])
        made.append(tarball)

    log("=== Linux packages done ===")
    for p in made:
        log("  %s (%d bytes)" % (p, p.stat().st_size))


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