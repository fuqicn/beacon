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


def run(cmd, cwd=None, env=None, shell=False, capture=False, check=True):
    if shell and isinstance(cmd, list):
        cmd = subprocess.list2cmdline(cmd)
    log("+ " + (cmd if shell else subprocess.list2cmdline(cmd)))
    full_env = os.environ.copy()
    if env:
        full_env.update(env)
    result = subprocess.run(cmd, cwd=cwd, env=full_env, shell=shell, check=False,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.STDOUT if capture else None)
    if result.returncode != 0:
        if capture:
            print(result.stdout.decode('utf-8', errors='replace'), end='')
        if check:
            raise PackError("command failed with exit code %d" % result.returncode)
        return None
    return result.stdout.decode('utf-8', errors='replace') if capture else None


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
    return m.group(1) if m else "1.0.2"


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


def _resolve_vcpkg_root(args):
    """Locate vcpkg installation for use as CMake toolchain.

    Checks: --vcpkg arg, VCPKG_ROOT env var, then common CI paths.
    Returns None if not found.
    """
    if args.vcpkg:
        p = Path(args.vcpkg)
        if (p / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
            return p
        raise PackError("vcpkg not found at %s (no vcpkg.cmake)" % p)
    env = os.environ.get("VCPKG_ROOT")
    if env:
        p = Path(env)
        if (p / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
            return p
    # Common GitHub Actions vcpkg locations
    for candidate in [
        Path(r"C:\vcpkg"),
        Path(r"C:\Program Files\vcpkg"),
    ]:
        if (candidate / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
            return candidate
    return None


def find_msvc_toolchain(arch=None):
    """Locate MSVC build tools (cl.exe, rc.exe, link.exe, optional strip).

    Args:
        arch: Target architecture - "x64" (default), "ARM64", or None.
              Used to select the correct tool subdirectory.

    Returns:
        A dict with keys 'cl', 'rc', 'link', 'strip' (Paths) or None if
        not available.
    """
    result = {"cl": None, "rc": None, "link": None, "strip": None}
    arch = (arch or "x64").lower()
    tool_subdir = "arm64" if "arm" in arch else "x64"

    # Find VS 2022 installation root and MSVC version directory.
    msvc_ver = None
    for base in [
        r"C:\Program Files\Microsoft Visual Studio\2022",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022",
    ]:
        for edition in ("Enterprise", "BuildTools", "Community"):
            vc_tools = Path(base) / edition / "VC" / "Tools" / "MSVC"
            if vc_tools.is_dir():
                versions = sorted(vc_tools.iterdir(), reverse=True)
                if versions:
                    msvc_ver = versions[0]
                    break
        if msvc_ver:
            break

    if msvc_ver:
        # Build toolchain dirs: prefer same-arch host, then cross-compile host.
        if tool_subdir == "x64":
            tool_dirs = [
                msvc_ver.parent / "bin" / "Hostx64" / "x64",
                msvc_ver.parent / "bin" / "Hostx64" / "arm64",
            ]
        else:
            tool_dirs = [
                msvc_ver.parent / "bin" / "Hostarm64" / "arm64",
                msvc_ver.parent / "bin" / "Hostx64" / "arm64",
                msvc_ver.parent / "bin" / "Hostx64" / "x64",
            ]
        for td in tool_dirs:
            cl_e = td / "cl.exe"
            rc_e = td / "rc.exe"
            link_e = td / "link.exe"
            if cl_e.is_file() and rc_e.is_file() and link_e.is_file():
                result["cl"] = cl_e
                result["rc"] = rc_e
                result["link"] = link_e
                db = td / "dumpbin.exe"
                if db.is_file():
                    result["strip"] = db
                log("using MSVC toolchain: %s" % td)
                return result

    # Last resort: hardcoded known CI paths.
    known = {
        "x64": [
            r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\*\bin\Hostx64\x64",
            r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64",
        ],
        "arm64": [
            r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\*\bin\Hostarm64\arm64",
            r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\*\bin\Hostx64\arm64",
            r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostarm64\arm64",
            r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\arm64",
        ],
    }
    import glob
    for pat in known.get(tool_subdir, []):
        dirs = sorted(glob.glob(pat))
        for d in dirs:
            td = Path(d)
            cl_e = td / "cl.exe"
            rc_e = td / "rc.exe"
            link_e = td / "link.exe"
            if cl_e.is_file() and rc_e.is_file() and link_e.is_file():
                result["cl"] = cl_e
                result["rc"] = rc_e
                result["link"] = link_e
                db = td / "dumpbin.exe"
                if db.is_file():
                    result["strip"] = db
                log("using MSVC toolchain (glob): %s" % td)
                return result

    return None


def _build_launcher(packaging, pack_tmp, dist, msvc, mingw_bin):
    """Build the C self-extractor launcher using the best available toolchain."""
    arch_tag = "amd64" if platform.machine().lower() in ("x86_64", "amd64") else platform.machine().lower()
    launcher = dist / ("BeaconLauncher-windows-%s.exe" % arch_tag)

    if msvc:
        # MSVC path: rc.exe compiles .rc → .res, cl.exe compiles .c, link.exe links.
        res = pack_tmp / "beacon.res"
        # Generate a minimal manifest resource (no icon in resource file; icon
        # is embedded via RC directives in beacon.rc).
        # rc.exe handles the .rc file directly.
        run([str(msvc["rc"]), "/fo", str(res), str(packaging / "beacon.rc")])
        run([str(msvc["cl"]), "/Fe:" + str(launcher),
             "/W3", "/O2", "/utf-8",
             "/DWIN32", "/D_WINDOWS", "/D_NDEBUG",
             "/I" + str(msvc["cl"].parent.parent.parent.parent / "include"),
             str(packaging / "main.c"), str(res),
             "/link", "/SUBSYSTEM:WINDOWS",
             "shell32.lib", "user32.lib", "gdi32.lib", "comctl32.lib",
             "/OUT:" + str(launcher)])
        log("compiled launcher with MSVC: %s" % launcher)
    else:
        # mingw fallback (local dev without VS)
        if not mingw_bin:
            raise PackError("no toolchain found: pass --mingw-bin or install VS Build Tools")
        windres = gcc = None
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
            raise PackError("windres/gcc not found in %s" % mingw_bin)
        res = pack_tmp / "beacon.res"
        run([str(windres), "-O", "coff", str(packaging / "beacon.rc"), "-o", str(res)])
        run([str(gcc), str(packaging / "main.c"), str(res), "-o", str(launcher),
             "-lshell32", "-luser32", "-lgdi32", "-lcomctl32", "-O2", "-s", "-mwindows"])
        log("compiled launcher with mingw: %s" % launcher)

    return launcher


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
    elif detect_platform() == "windows":
        # CI: use VS generator for reliable MSVC detection.
        # Local dev: Ninja is faster for incremental builds; falls back to
        # whatever CMake finds on PATH (mingw, ninja, etc.).
        use_msvc = getattr(args, 'msvc', False)
        if use_msvc:
            cfg.append("-G")
            cfg.append("Visual Studio 17 2022")
            cfg.append("-A")
            cfg.append("ARM64" if getattr(args, 'arch', None) == "arm64" else "x64")
            # Use vcpkg for dependencies that MSVC can't find natively (e.g. zlib).
            vcpkg_root = _resolve_vcpkg_root(args)
            if vcpkg_root:
                cfg.append("-DCMAKE_TOOLCHAIN_FILE=%s" % (vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake"))
                log("using vcpkg toolchain: %s" % vcpkg_root)
        else:
            cfg.append("-G")
            cfg.append("Ninja")
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
    elif detect_platform() == "darwin" and getattr(args, 'osx_arch', None):
        # Cross-compile for specific macOS architecture.
        cfg.append("-DCMAKE_OSX_ARCHITECTURES=%s" % args.osx_arch)
    if not build_dir.exists():
        run(cfg)
    else:
        run(cfg)
    run([str(cmake), "--build", str(build_dir), "--target", "Beacon",
         "--parallel", str(args.jobs)] +
        (["--config", "Release"] if getattr(args, "msvc", False) else []))


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

    # VS generator puts output in build/<arch>/<config>/ or build/<config>/ (depending on cmake version).
    arch_tag = "arm64" if getattr(args, "arch", None) == "arm64" else "x64"
    candidates = [
        Path(build_dir) / arch_tag / "Release" / "Beacon.exe",
        Path(build_dir) / "Release" / "Beacon.exe",
        Path(build_dir) / arch_tag / "Debug" / "Beacon.exe",
        Path(build_dir) / "Debug" / "Beacon.exe",
        Path(build_dir) / "Beacon.exe",
    ]
    exe = next((p for p in candidates if p.is_file()), None)
    if exe is None:
        raise PackError("Beacon.exe not found in %s" % build_dir)
    shutil.copy2(exe, beacon_dir / "Beacon.exe")
    log("copied Beacon.exe -> %s" % (beacon_dir / "Beacon.exe"))

    # Extract MSVC toolchain from CMakeCache (already configured by configure_and_build).
    # This is more reliable than re-scanning disk, since cmake already found the right paths.
    msvc = None
    if getattr(args, "msvc", False):
        cache = read_cmake_cache(build_dir)
        cxx = _clean_cache_value(cache.get("CMAKE_CXX_COMPILER"))
        if cxx and Path(cxx).is_file():
            compiler_dir = Path(cxx).parent
            msvc = {"cl": Path(cxx), "rc": compiler_dir / "rc.exe", "link": compiler_dir / "link.exe"}
            dumpbin = compiler_dir / "dumpbin.exe"
            if dumpbin.is_file():
                msvc["strip"] = dumpbin
            log("using MSVC toolchain from cmake cache: %s" % compiler_dir)
        else:
            log("WARNING: CMAKE_CXX_COMPILER not found in cmake cache, falling back to disk scan")
            msvc = find_msvc_toolchain(getattr(args, "arch", None))
    if not msvc:
        msvc = find_msvc_toolchain(getattr(args, "arch", None))
    mingw_bin = resolve_mingw_bin(args, build_dir)

    # Strip the deployed binary to cut package size. The unstripped original
    # stays in build/ so crash.log symbolization keeps working in dev builds.
    strip = None
    if msvc:
        strip = msvc.get("strip")
    elif mingw_bin:
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
        log("WARNING: strip tool not found; Beacon.exe not stripped")

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
    launcher = _build_launcher(packaging, pack_tmp, dist, msvc, mingw_bin)

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


def map_arch(override=None):
    """Map host machine to (deb_arch, rpm_arch, pacman_arch).

    When *override* is given (e.g. "arm64") it is returned directly as all
    three keys so the caller can control the output suffix explicitly.
    """
    if override:
        return {"deb": override, "rpm": override, "pacman": override}
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


def stage_install_tree(args, version, build_dir, install_prefix=None):
    """Install the build tree into a staging prefix and add desktop metadata."""
    cmake = resolve_cmake(build_dir)
    if install_prefix is not None:
        staging = Path(install_prefix)
    else:
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


def build_deb(staging, version, dist, arch_override=None):
    """Build the Debian package via dpkg-deb."""
    if not shutil.which("dpkg-deb"):
        log("WARNING: dpkg-deb not found; skipping .deb package")
        return None
    arch = map_arch(arch_override)["deb"]
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


def build_rpm(staging, version, dist, arch_override=None):
    """Build the RPM package via rpmbuild."""
    rpmbuild = shutil.which("rpmbuild")
    if not rpmbuild:
        log("WARNING: rpmbuild not found; skipping .rpm package")
        return None
    arch = map_arch(arch_override)["rpm"]
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


def build_arch_pkg(staging, version, dist, arch_override=None):
    """Build the Arch package via makepkg."""
    if not shutil.which("makepkg"):
        log("WARNING: makepkg not found; skipping Arch package")
        return None
    arch = map_arch(arch_override)["pacman"]
    work = staging.parent / "arch-pkg"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    (work / "PKGBUILD").write_text(
        PKGBUILD_TEMPLATE
        .replace("@VERSION@", version)
        .replace("@ARCH@", arch)
        .replace("@HOMEPAGE@", HOMEPAGE)
        .replace("@STAGING@", str(Path(staging).resolve()))
        .replace("@BIN@", BIN_NAME)
        .replace("@APPID@", APP_ID),
        encoding="utf-8")

    # makepkg refuses to run as root. If we're root (e.g. inside a Docker
    # container), create a temporary non-root user and run makepkg via su.
    cmd = ["makepkg", "-f", "-d"]   # -f force overwrite, -d skip dep check
    if os.geteuid() == 0:
        user = "archbuilder"
        # Create user first if it doesn't exist (id will fail otherwise).
        has_id = shutil.which("id") and run(["id", "-u", user], capture=True, check=False)
        if not has_id or has_id.strip() == "":
            run(["useradd", "-m", "-s", "/bin/bash", user])
        run(["chown", "-R", "%s:%s" % (user, user), str(work)])
        run("su - %s -c \"cd %s && makepkg -f -d\"" % (user, str(work.resolve())), shell=True)
        produced = sorted(work.glob("*.pkg.tar.*"))
    else:
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

    made = [p for p in (build_deb(staging, version, dist, args.arch),
                        build_rpm(staging, version, dist, args.arch),
                        build_arch_pkg(staging, version, dist, args.arch)) if p]

    if not made:
        # No packaging tooling available — fall back to an installable tarball.
        tarball = dist / ("beacon-%s-linux-%s.tar.gz"
                          % (version, map_arch(args.arch)["pacman"]))
        run(["tar", "-czf", str(tarball), "-C", str(staging), "usr"])
        made.append(tarball)

    log("=== Linux packages done ===")
    for p in made:
        log("  %s (%d bytes)" % (p, p.stat().st_size))


def _find_qt_on_macos():
    """Locate the Homebrew-installed Qt 6 root directory on macOS."""
    for prefix in ("/opt/homebrew", "/usr/local"):
        cand = Path(prefix) / "opt" / "qt@6"
        if cand.is_dir():
            return cand
    env = os.environ.get("QT_DIR")
    if env and Path(env).is_dir():
        return Path(env)
    return None


def build_macos(args, version, qt_dir=None, arch_suffix="arm64"):
    """Build and package a macOS .app bundle."""
    log("=== Building Beacon for macOS (%s) ===" % arch_suffix)
    build_dir = Path(args.build_dir) if args.build_dir else (ROOT / "build-mac-%s" % arch_suffix)
    # macOS may run two architectures sequentially in CI; use a separate dist dir
    # so they don't overwrite each other.
    dist = Path(args.mac_dist_dir) if args.mac_dist_dir else Path(args.dist_dir)
    if not args.skip_build:
        configure_and_build(args, build_dir, qt_dir)
    binary = build_dir / "Beacon.app"
    if not binary.is_dir():
        raise PackError("Beacon.app not found in %s" % build_dir)

    dist = Path(args.dist_dir)
    dist.mkdir(parents=True, exist_ok=True)

    # Stage: install into a clean staging tree that mirrors .app contents.
    staging = build_dir / "mac-staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    cmake = resolve_cmake(build_dir)
    # Install to staging — CMake's MACOSX_BUNDLE creates Beacon.app at the prefix root.
    run([str(cmake), "--install", str(build_dir), "--prefix", str(staging)])

    # Strip the binary.
    staged_app = staging / "Beacon.app" / "MacOS" / BIN_NAME
    strip = shutil.which("strip")
    if strip and staged_app.is_file():
        before = staged_app.stat().st_size
        run([str(strip), "--strip-all", str(staged_app)])
        log("stripped %s: %d -> %d bytes"
            % (staged_app, before, staged_app.stat().st_size))

    # Deploy Qt frameworks and plugins via macdeployqt if available.
    qt_root = Path(qt_dir) if qt_dir else _find_qt_on_macos()
    if qt_root:
        macdeployqt = qt_root / "bin" / "macdeployqt"
        if macdeployqt.is_file():
            log("--- Deploying Qt runtime (macdeployqt) ---")
            run([str(macdeployqt), str(staging / "Beacon.app"),
                 "-always-overwrite"])
        else:
            log("WARNING: macdeployqt not found at %s" % macdeployqt)
    else:
        log("WARNING: Qt root not found on macOS; frameworks may be incomplete")

    # Copy runtime assets.
    resources = staging / "Beacon.app" / "Resources"
    resources.mkdir(parents=True, exist_ok=True)
    copy_mirrors_json(resources / "mirrors.json")
    write_version_file(resources / "version.txt", version)

    # Final .app lives in dist/.
    final_app = dist / ("Beacon-%s.app" % arch_suffix)
    if final_app.exists():
        shutil.rmtree(final_app)
    shutil.move(str(staging / "Beacon.app"), str(final_app))
    shutil.rmtree(staging, ignore_errors=True)

    log("=== macOS package done ===")
    log("  dist/Beacon-%s.app (%d bytes)" % (arch_suffix,
        sum(f.stat().st_size for f in final_app.rglob("*") if f.is_file())))


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
    p.add_argument("--arch", default=None,
                   help="override architecture suffix (e.g. arm64, x86_64); defaults to host machine arch")
    p.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                   help="parallel build jobs (default: cpu count)")
    p.add_argument("--msvc", action="store_true",
                   help="use Visual Studio generator instead of Ninja (CI)")
    p.add_argument("--vcpkg", default=None,
                   help="path to vcpkg root (auto-detected from VCPKG_ROOT env or common paths)")
    p.add_argument("--skip-build", action="store_true",
                   help="skip configure/build (requires existing build artifacts)")
    p.add_argument("--osx-arch", choices=["arm64", "x86_64"], default="arm64",
                   help="macOS target architecture (default: arm64)")
    p.add_argument("--mac-dist-dir", default=None,
                   help="macOS dist output directory (overrides --dist-dir for macOS builds only; "
                        "use to avoid overwriting between arm64 and x86_64 builds)")
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
        build_dir = Path(args.build_dir) if args.build_dir else ROOT / "build-mac-arm64"
        qt_dir = args.qt_dir or os.environ.get("QT_DIR")
        build_macos(args, version, qt_dir, arch_suffix=args.osx_arch)


if __name__ == "__main__":
    try:
        main()
    except PackError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print("ERROR: command failed with exit code %d" % e.returncode, file=sys.stderr)
        sys.exit(e.returncode)