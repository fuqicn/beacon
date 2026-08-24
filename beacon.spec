Name:           beacon
Version:        1.0.1
Release:        0
Summary:        Cross-platform Minecraft launcher
License:        GPL-3.0-or-later
URL:            https://github.com/fuqicn/beacon
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
# Qt6 dependencies
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtquickcontrols2-devel
BuildRequires:  qt6-qtsvg-devel
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  zlib-devel
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtquickcontrols2
Requires:       qt6-qtsvg
Requires:       zlib

%description
Beacon is a cross-platform Minecraft launcher with support for mods, modpacks,
and multiple instances.

%prep
%setup -q

%build
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel %{_smp_mflags}

%install
cmake --install build --prefix %{buildroot}/usr

%files
/usr/bin/Beacon
/usr/share/applications/io.github.fuqicn.beacon.desktop
/usr/share/icons/hicolor/scalable/apps/io.github.fuqicn.beacon.svg
/usr/share/beacon/mirrors.json

%changelog
* Mon Aug 25 2025 fuqicn <fuqi2012cn@outlook.com> - 1.0.1-0
- Initial package for openSUSE Build Service
