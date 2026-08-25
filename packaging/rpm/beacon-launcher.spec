# RPM spec for Beacon Launcher (Fedora / RHEL / openSUSE).
# Consumed by OBS; source is the GitHub tag tarball "v-<version>".
Name:           beacon-launcher
Version:        1.0.1
Release:        1%{?dist}
Summary:        A cross-platform Minecraft launcher

License:        GPL-3.0-or-later
URL:            https://github.com/fuqicn/beacon
Source0:        %{url}/archive/refs/tags/v-%{version}/beacon-v-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  qt6-qtbase-devel >= 6.4
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  zlib-devel

Requires:       qt6-qtbase >= 6.4
Requires:       qt6-qtdeclarative
Requires:       qt6-qtsvg
Requires:       zlib

%description
Beacon is a Qt 6 based Minecraft launcher featuring Microsoft and offline
login, version/mod/modpack downloads and per-instance management.

%prep
%autosetup -n beacon-v-%{version}

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLINUX_NO_CACHEGEN=ON
%cmake_build

%install
%cmake_install

%files
%{_bindir}/Beacon
%{_datadir}/applications/io.github.fuqicn.beacon.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg
%dir %{_datadir}/beacon
%{_datadir}/beacon/mirrors.json
%{_datadir}/beacon/version.txt

%changelog
* Mon Aug 24 2026 fuqicn <fuqi2012cn@outlook.com> - 1.0.1-1
- Upstream release 1.0.1
