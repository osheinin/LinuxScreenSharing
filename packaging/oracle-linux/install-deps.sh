#!/usr/bin/env bash
set -euo pipefail
. /etc/os-release
if [[ "$ID" != "ol" ]]; then
  echo "This dependency installer requires Oracle Linux." >&2
  exit 1
fi
case "${VERSION_ID%%.*}" in
  7)
    yum install -y oracle-softwarecollection-release-el7 oracle-epel-release-el7 yum-utils
    yum-config-manager --enable ol7_software_collections ol7_developer_EPEL ol7_optional_latest
    yum install -y devtoolset-11-gcc devtoolset-11-gcc-c++ cmake3
    ;;
  8|9)
    dnf install -y dnf-plugins-core
    dnf install -y "oracle-epel-release-el${VERSION_ID%%.*}"
    dnf config-manager --set-enabled "ol${VERSION_ID%%.*}_codeready_builder"
    dnf config-manager --set-enabled "ol${VERSION_ID%%.*}_developer_EPEL"
    dnf install -y gcc gcc-c++ cmake
    ;;
  *) echo "Supported releases: Oracle Linux 7, 8, 9" >&2; exit 1 ;;
esac
yum install -y make tar gzip pkgconfig \
  zlib-devel pixman-devel libjpeg-turbo-devel gnutls-devel \
  libX11-devel libXext-devel libXtst-devel libXdamage-devel \
  libXfixes-devel libXrandr-devel \
  xorg-x11-server-Xvfb xclip python3 openssl
