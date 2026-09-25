# SPDX-License-Identifier: GPL-2.0-or-later
%global debug_package %{nil}
%global app_prefix /opt/tigervnc-apple

Name:           tigervnc-apple
Version:        1.16.80
Release:        1%{?dist}
Summary:        TigerVNC X11 server with macOS text clipboard interoperability
License:        GPL-2.0-or-later AND MIT
URL:            https://github.com/osheinin/LinuxScreenSharing
Source0:        %{name}-%{version}.tar.gz
ExclusiveArch:  x86_64
Requires:       oraclelinux-release
Requires:       system-release(releasever) = 9
BuildRequires:  gcc, gcc-c++, cmake, make
BuildRequires:  zlib-devel, pixman-devel, libjpeg-turbo-devel, gnutls-devel
BuildRequires:  libX11-devel, libXext-devel, libXtst-devel
BuildRequires:  libXdamage-devel, libXfixes-devel, libXrandr-devel
BuildRequires:  python3, openssl, xorg-x11-server-Xvfb, xclip

%description
Shares an existing X11 desktop on Oracle Linux 9 and adds optional UTF-8
plain-text clipboard exchange with the built-in macOS Screen Sharing client.
Installs in /opt/tigervnc-apple alongside the distribution's TigerVNC.
Run as the desktop user with -AppleClipboard and connect through an SSH tunnel.
This package does not create a desktop session or enable a network service.

%prep
%setup -q

%build
cmake -S . -B build-rpm \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="%{optflags}" -DCMAKE_CXX_FLAGS="%{optflags}" \
  -DCMAKE_INSTALL_PREFIX=%{app_prefix} \
  -DBUILD_VIEWER=OFF -DENABLE_WAYLAND=OFF -DENABLE_NLS=OFF \
  -DENABLE_H264=OFF -DENABLE_AUDIO=OFF -DENABLE_PAM=OFF \
  -DENABLE_SYSTEMD=OFF -DENABLE_PWQUALITY=OFF \
  -DENABLE_GNUTLS=ON -DENABLE_NETTLE=OFF
cmake --build build-rpm --target x0vncserver vncpasswd \
  apple_clipboard_test apple_protocol_test --parallel %{_smp_build_ncpus}

%check
ctest --test-dir build-rpm --output-on-failure -R '^apple_'
python3 tests/apple/x11_clipboard_smoke.py build-rpm

%install
install -Dpm 0755 build-rpm/unix/x0vncserver/x0vncserver \
  %{buildroot}%{app_prefix}/bin/x0vncserver
install -Dpm 0755 build-rpm/unix/vncpasswd/vncpasswd \
  %{buildroot}%{app_prefix}/bin/vncpasswd
install -Dpm 0644 unix/x0vncserver/x0vncserver.man \
  %{buildroot}%{app_prefix}/share/man/man1/x0vncserver.1
install -Dpm 0644 unix/vncpasswd/vncpasswd.man \
  %{buildroot}%{app_prefix}/share/man/man1/vncpasswd.1

%files
%license LICENCE.TXT README.rst
%doc README.apple.md docs/apple-clipboard.md packaging/oracle-linux/README.rpm.md
%doc SOURCE_REVISION
%{app_prefix}/

%changelog
* Fri Sep 25 2026 LinuxScreenSharing contributors - 1.16.80-1
- Initial Oracle Linux 9 RPM with Apple clipboard support and source RPM.
