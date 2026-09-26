#!/usr/bin/env bash
set -euo pipefail
. /etc/os-release
if [[ "$ID" != "ol" || ! "${VERSION_ID%%.*}" =~ ^[789]$ ]]; then
  echo "Run this build on Oracle Linux 7, 8 or 9." >&2
  exit 1
fi
release="${VERSION_ID%%.*}"
cmake_bin=cmake
ctest_bin=ctest
if [[ "$release" == 7 ]]; then
  # OL7's system GCC is too old for the current upstream build flags.
  set +u
  source /opt/rh/devtoolset-11/enable
  set -u
  cmake_bin=cmake3
  ctest_bin=ctest3
fi
source_dir="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${BUILD_DIR:-$source_dir/build-ol$release}"
output_dir="${OUTPUT_DIR:-$source_dir/dist}"
"$cmake_bin" -S "$source_dir" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/tigervnc-apple \
  -DBUILD_VIEWER=OFF -DENABLE_WAYLAND=OFF -DENABLE_NLS=OFF \
  -DENABLE_H264=OFF -DENABLE_AUDIO=OFF -DENABLE_PAM=OFF \
  -DENABLE_SYSTEMD=OFF -DENABLE_PWQUALITY=OFF \
  -DENABLE_GNUTLS=ON -DENABLE_NETTLE=OFF
"$cmake_bin" --build "$build_dir" --target \
  x0vncserver vncpasswd apple_clipboard_test apple_protocol_test --parallel "${JOBS:-2}"
# CTest 3.17 (OL7) does not support --test-dir.
(cd "$build_dir" && "$ctest_bin" --output-on-failure -R '^apple_')
python3 "$source_dir/tests/apple/x11_clipboard_smoke.py" "$build_dir"
stage="$build_dir/apple-package"
prefix="$stage/opt/tigervnc-apple"
mkdir -p "$prefix/bin" "$prefix/share/man/man1" "$prefix/share/doc" "$output_dir"
install -m 0755 "$build_dir/unix/x0vncserver/x0vncserver" "$prefix/bin/"
install -m 0755 "$build_dir/unix/vncpasswd/vncpasswd" "$prefix/bin/"
install -m 0644 "$source_dir/unix/x0vncserver/x0vncserver.man" "$prefix/share/man/man1/x0vncserver.1"
install -m 0644 "$source_dir/LICENCE.TXT" "$source_dir/README.apple.md" "$prefix/share/doc/"
tar -C "$stage" -czf "$output_dir/tigervnc-apple-ol$release-$(uname -m).tar.gz" opt
echo "Package written to $output_dir (system packages were not replaced)."
