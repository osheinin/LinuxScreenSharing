# Oracle Linux 8 and 9 RPMs

This package targets **Oracle Linux 8 or 9 x86_64** with an existing Xorg/X11 desktop.
Use the `.el8` RPM on OL8 and the `.el9` RPM on OL9; the packages enforce this
OS version requirement. It is a development build of the TigerVNC Apple clipboard fork (1.16.80).

## Install

Download the binary RPM and SHA256SUMS from the same build. Verify the binary's
checksum (the manifest also lists the source RPM):

```sh
sha256sum --check --ignore-missing SHA256SUMS
# Oracle Linux 8:
sudo dnf install ./tigervnc-apple-1.16.80-2.el8.x86_64.rpm
# Oracle Linux 9:
# sudo dnf install ./tigervnc-apple-1.16.80-2.el9.x86_64.rpm
```

DNF installs the required runtime libraries from your configured Oracle
repositories. Offline installation requires those dependencies to be installed
already or supplied through a local repository. The RPM is unsigned; checksums
detect corruption but are not a publisher signature. On systems requiring signed
local packages, have your administrator sign it with an approved key.

The files install under `/opt/tigervnc-apple`. Before installing, relocate any
previous manual tarball installation at that path so RPM does not overwrite
unmanaged files. The package has no service scripts, creates no accounts and
does not change the firewall or the distribution's TigerVNC.

## Start from the Linux desktop

Select an Xorg session at login. Open a terminal in that session and run as the
desktop user, without sudo:

```sh
mkdir -p ~/.vnc
chmod 700 ~/.vnc
/opt/tigervnc-apple/bin/vncpasswd ~/.vnc/passwd-apple
/opt/tigervnc-apple/bin/x0vncserver \
  -display "$DISPLAY" -rfbport 5901 -localhost \
  -PasswordFile "$HOME/.vnc/passwd-apple" \
  -SecurityTypes VncAuth -AppleClipboard
```

On the Mac:

```sh
ssh -N -L 5901:127.0.0.1:5901 your-user@your-linux-host
open vnc://localhost:5901
```

Enable **Edit → Use Shared Clipboard** in Screen Sharing. Clipboard support is
plain text, including Unicode. The SSH tunnel encrypts the otherwise unencrypted
VNC connection. Stop the foreground server with Ctrl+C.

## Remove or rebuild

Stop the server, then run `sudo dnf remove tigervnc-apple`. Your personal password
file remains in `~/.vnc`.

The matching `.src.rpm` contains the complete source archive and RPM spec. On an
matching OL8 or OL9 build machine with the fork's development repositories configured:

```sh
sudo dnf install rpm-build dnf-plugins-core
# OL8 example; use el9 and rhel 9 on OL9:
sudo dnf builddep --define "rhel 8" ./tigervnc-apple-1.16.80-2.el8.src.rpm
rpmbuild --rebuild --define "rhel 8" --define "dist .el8" \
  ./tigervnc-apple-1.16.80-2.el8.src.rpm
```

See `packaging/oracle-linux/install-deps.sh` in the source for repository setup.
The source archive includes `SOURCE_REVISION` identifying the fork commit.
The binary RPM includes the GPL and upstream third-party notices in its license
directory. Keep the source RPM available alongside the binary when redistributing.

CI runs protocol tests, tests both clipboard directions under Xvfb, installs the
RPM in a fresh container of its target Oracle Linux release, repeats the clipboard test against installed binaries,
and tests removal. This does not replace testing a full GNOME desktop session.
