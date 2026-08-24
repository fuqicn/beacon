#!/usr/bin/env python3
"""Build Linux packages: tar.gz, .deb, and Arch package."""
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DIST_DIR = ROOT / "dist"
BUILD_DIR = ROOT / "build-linux"


def log(msg):
    print(msg, flush=True)


def run(cmd, cwd=None):
    log("+ " + (" ".join(str(x) for x in cmd)))
    subprocess.run(cmd, cwd=cwd, check=True)


def build_linux_package(version, skip_build=False):
    """Build and package for Linux (tar.gz + .deb + Arch)."""
    log("=== Building Beacon for Linux ===")

    # Build
    if not skip_build:
        cmake = shutil.which("cmake")
        if not cmake:
            raise RuntimeError("cmake not found (set CMAKE env var)")
        build_dir = BUILD_DIR
        build_dir.mkdir(parents=True, exist_ok=True)
        qt_dir = os.environ.get("QT_DIR") or os.environ.get("QT_QMAKE_EXECUTABLE", "").replace("qmake", "").strip()
        cfg = [cmake, "-B", str(build_dir), "-S", str(ROOT),
               "-DCMAKE_BUILD_TYPE=Release",
               "-DCMAKE_INSTALL_PREFIX=/usr",
               "-DLINUX_NO_CACHEGEN=ON"]
        if qt_dir:
            cfg.append("-DCMAKE_PREFIX_PATH=%s" % qt_dir)
        run(cfg)
        run([cmake, "--build", str(build_dir), "--target", "Beacon", "--parallel"])

    binary = BUILD_DIR / "Beacon"
    if not binary.exists():
        raise RuntimeError("Binary not found: %s" % binary)

    DIST_DIR.mkdir(parents=True, exist_ok=True)

    # ── tar.gz ──────────────────────────────────────────────────
    staging = BUILD_DIR / "linux-staging" / ("beacon-%s" % version)
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    shutil.copy2(binary, staging / "Beacon")
    os.chmod(staging / "Beacon", 0o755)
    log("copied Beacon -> staging/")

    mirrors_src = ROOT / "third_party" / "minecraft-launcher-kernel" / "mirrors.json"
    if mirrors_src.exists():
        shutil.copy2(mirrors_src, staging / "mirrors.json")
        log("copied mirrors.json")

    svg_src = ROOT / "Untitled.svg"
    if svg_src.exists():
        shutil.copy2(svg_src, staging / "beacon.svg")
        log("copied icon")

    desktop = staging / "io.github.fuqicn.beacon.desktop"
    desktop.write_text("""[Desktop Entry]
Name=Beacon
Exec=%(dir)s/Beacon
Icon=%(dir)s/beacon.svg
Type=Application
Categories=Game;
Comment=A cross-platform Minecraft launcher
""" % {"dir": "."}, encoding="utf-8")

    tarball = DIST_DIR / ("beacon-%s-linux.tar.gz" % version)
    run(["tar", "czf", str(tarball), "-C", str(staging.parent), staging.name])
    log("Linux tarball: %s (%d bytes)" % (tarball, tarball.stat().st_size))

    # ── .deb package ────────────────────────────────────────────
    deb_staging = staging / "debian"
    deb_staging.mkdir(parents=True)

    # debian/control
    deb_control = deb_staging / "control"
    deb_control.write_text("""Source: beacon
Section: games
Priority: optional
Maintainer: fuqicn <fuqicn@github.com>
Build-Depends: debhelper-compat (= 13), cmake (>= 3.16), ninja-build (>= 1.10)
Standards-Version: 4.6.0
Rules-Requires-Root: no

Package: beacon
Architecture: all amd64 arm64
Depends: $ {shlibs:Depends}, $ {misc:Depends}, libqt6core6a (>= 6.5), libqt6gui6a (>= 6.5), libqt6qml6a (>= 6.5), libqt6quick6a (>= 6.5), libqt6quickcontrols2-6a (>= 6.5), libqt6svg6 (>= 6.5), libqt6widgets6a (>= 6.5), libc6 (>= 2.31), libstdc++6 (>= 11)
Description: A cross-platform Minecraft launcher
 Beacon is a modern Minecraft launcher built with Qt 6.
 It supports multiple instances, mod management, and more.
""", encoding="utf-8")

    # debian/rules
    deb_rules = deb_staging / "rules"
    deb_rules.write_text(r"""#!/usr/bin/make -f
%:
\tdh $@

override_dh_auto_build:
	cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DLINUX_NO_CACHEGEN=ON
	cmake --build build --target Beacon -j$$(nproc)

override_dh_auto_install:
	install -D -m 755 build/Beacon $(CURDIR)/debian/beacon/usr/bin/Beacon
	install -D -m 644 third_party/minecraft-launcher-kernel/mirrors.json $(CURDIR)/debian/beacon/usr/share/beacon/mirrors.json
	install -D -m 644 Untitled.svg $(CURDIR)/debian/beacon/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg
	install -D -m 644 debian/io.github.fuqicn.beacon.desktop $(CURDIR)/debian/beacon/usr/share/applications/io.github.fuqicn.beacon.desktop
	touch debian/beacon/usr/share/beacon/.keep

binary-indep: binary-arch

.PHONY: override_dh_auto_build override_dh_auto_install
""", encoding="utf-8")
    os.chmod(deb_rules, 0o755)

    # debian/changelog
    from datetime import datetime
    deb_changelog = deb_staging / "changelog"
    deb_changelog.write_text("""beacon (%s) stable; urgency=low

  * Initial release.

 -- fuqicn <fuqicn@github.com>  %s +0000
""" % (version, datetime.utcnow().strftime("%a, %d %b %Y %H:%M:%S +0000")), encoding="utf-8")

    # debian/copyright (DEP-5)
    deb_copyright = deb_staging / "copyright"
    deb_copyright.write_text("""Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: beacon
Upstream-Contact: fuqicn <fuqicn@github.com>
Source: https://github.com/fuqicn/beacon

Files: *
Copyright: 2024-2026 fuqicn
License: GPL-3+

Files: third_party/*
Copyright: see individual files
License: see individual files

License: GPL-3+
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 .
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 .
 On Debian systems, the complete text of the GNU General Public
 License version 3 can be found in "/usr/share/common-licenses/GPL-3".
""", encoding="utf-8")

    # debian/source/format
    (deb_staging / "source").mkdir(parents=True, exist_ok=True)
    (deb_staging / "source" / "format").write_text("3.0 (native)\n", encoding="utf-8")

    # Build .deb
    deb_output = DIST_DIR / ("beacon_%s_all.deb" % version)
    run(["dpkg-buildpackage", "-us", "-uc", "-b"], cwd=str(deb_staging))
    if deb_output.exists():
        log("Debian package: %s (%d bytes)" % (deb_output, deb_output.stat().st_size))
    else:
        # dpkg-buildpackage outputs to parent dir
        for f in deb_staging.parent.glob("beacon_%s*.deb" % version):
            shutil.copy2(f, deb_output)
            log("Debian package: %s (%d bytes)" % (deb_output, deb_output.stat().st_size))
            break

    # ── Arch Linux package (PKGBUILD) ──────────────────────────
    arch_staging = staging / "arch"
    arch_staging.mkdir(parents=True)

    pkgbuild = arch_staging / "PKGBUILD"
    pkgbuild.write_text(r"""# Maintainer: fuqicn <fuqicn@github.com>
pkgname=beacon
pkgver=VERSION_PLACEHOLDER
pkgrel=1
pkgdesc="A cross-platform Minecraft launcher built with Qt 6"
arch=('x86_64' 'aarch64')
url="https://github.com/fuqicn/beacon"
license=('GPL3')
makedepends=('cmake' 'ninja' 'qt6-base' 'qt6-declarative' 'qt6-svg')
depends=('qt6-base' 'qt6-declarative' 'qt6-svg')
optdepends=('qt6-controls-style-fusion: Fusion style support')
source=("https://github.com/fuqicn/beacon/releases/download/v${pkgver}/${pkgname}-${pkgver}-linux.tar.gz")
sha256sums=('SKIP')

package() {
    # Install binary
    install -Dm755 "${pkgname}" "${pkgdir}/usr/bin/${pkgname}"

    # Install mirrors.json
    install -Dm644 "mirrors.json" "${pkgdir}/usr/share/${pkgname}/mirrors.json"

    # Install icon
    install -Dm644 "beacon.svg" "${pkgdir}/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg"

    # Install desktop file
    install -Dm644 "io.github.fuqicn.beacon.desktop" "${pkgdir}/usr/share/applications/io.github.fuqicn.beacon.desktop"
}
""", encoding="utf-8")
    pkgbuild.write_text(pkgbuild.read_text(encoding="utf-8").replace("VERSION_PLACEHOLDER", version), encoding="utf-8")
    log("Arch PKGBUILD: arch/%s" % pkgbuild.name)

    log("=== Linux build done ===")


if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument("--version", default="1.0.0")
    p.add_argument("--skip-build", action="store_true")
    args = p.parse_args()
    build_linux_package(args.version, args.skip_build)
