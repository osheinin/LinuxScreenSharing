# Oracle Linux 9 RPM

This package targets **Oracle Linux 9 x86_64** with an existing Xorg/X11 desktop.
It is a development build of the TigerVNC Apple clipboard fork (1.16.80).

## Install

Download the binary RPM and SHA256SUMS from the same build. Verify the binary's
checksum (the manifest also lists the source RPM):

```sh
sha256sum --check --ignore-missing SHA256SUMS
sudo dnf install ./tigervnc-apple-1.16.80-1.el9.x86_64.rpm
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
ssh -N -L 5901:127.0.0.1:5901 your-user@your-ol9-host
open vnc://localhost:5901
```

Enable **Edit → Use Shared Clipboard** in Screen Sharing. Clipboard support is
plain text, including Unicode. The SSH tunnel encrypts the otherwise unencrypted
VNC connection. Stop the foreground server with Ctrl+C.

## Remove or rebuild

Stop the server, then run `sudo dnf remove tigervnc-apple`. Your personal password
file remains in `~/.vnc`.

The matching `.src.rpm` contains the complete source archive and RPM spec. On an
OL9 build machine with the fork's development repositories configured:

```sh
sudo dnf install rpm-build dnf-plugins-core
sudo dnf builddep ./tigervnc-apple-1.16.80-1.el9.src.rpm
rpmbuild --rebuild ./tigervnc-apple-1.16.80-1.el9.src.rpm
```

See `packaging/oracle-linux/install-deps.sh` in the source for repository setup.
The source archive includes `SOURCE_REVISION` identifying the fork commit.
The binary RPM includes the GPL and upstream third-party notices in its license
directory. Keep the source RPM available alongside the binary when redistributing.

CI runs protocol tests, tests both clipboard directions under Xvfb, installs the
RPM in a fresh OL9 container, repeats the clipboard test against installed binaries,
and tests removal. This does not replace testing a full GNOME desktop session.
