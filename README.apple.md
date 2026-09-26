# TigerVNC with Apple Screen Sharing clipboard support

This GPL fork adds bidirectional **UTF-8 plain-text copy/paste** for the standard
macOS **Screen Sharing.app**. The base is TigerVNC commit
`ecbab04621580b9c38a54275a925dce47d017801` (upstream development version 1.16.80).
The compatibility option is **off by default**: enable `-AppleClipboard`.

Target platforms: **Oracle Linux 7, 8 and 9**, initially using X11.
The supplied package builds `x0vncserver`, which shares an existing X desktop.
The protocol changes also reside in the shared library used by Xvnc, but building
Xvnc itself requires a separate compatible Xorg source tree; see `BUILDING.txt`.
An existing Xvnc/Xvfb display can be shared through this `x0vncserver` as well.
On OL8/9, choose an Xorg desktop session for this package.

## Oracle Linux 8 and 9 RPMs

For an installable x86_64 RPM and matching source RPM, see
[the RPM guide](packaging/oracle-linux/README.rpm.md). The `Oracle Linux RPM`
workflow builds binary and source RPMs for each release and tests installation,
clipboard exchange, and removal on the matching OS.

## Build on Oracle Linux

From this source directory, install the dependencies and build:

```sh
sudo bash packaging/oracle-linux/install-deps.sh
bash packaging/oracle-linux/build.sh
```

The dependency script enables Oracle's development/EPEL repositories and installs
Xvfb/xclip for the integration test. OL7 uses
Devtoolset 11 and CMake 3 from Oracle's Software Collections/EPEL repositories;
OL8/9 use their standard compiler and CMake. Build separately on each target
release. The output is `dist/tigervnc-apple-olVERSION-ARCH.tar.gz` and installs
under `/opt/tigervnc-apple`, independently of the distribution's TigerVNC.

Alternatively, on a Linux Docker host:

```sh
docker build --build-arg OL_VERSION=9 \
  -f packaging/oracle-linux/Dockerfile --output type=local,dest=dist .
```

Repeat with `OL_VERSION=7` and `8`. The `Oracle Linux Apple clipboard` GitHub
Actions workflow builds all three x86_64 packages, runs the protocol tests, and
checks bidirectional X11 clipboard exchange using Xvfb and xclip.

## Run and connect

Extract the package built for the matching OS/architecture:

```sh
sudo tar -xzf dist/tigervnc-apple-ol9-x86_64.tar.gz -C /
mkdir -p ~/.vnc
/opt/tigervnc-apple/bin/vncpasswd ~/.vnc/passwd-apple
```

Run as the owner of the X desktop, with its `DISPLAY` and `XAUTHORITY` available:

```sh
/opt/tigervnc-apple/bin/x0vncserver \
  -display "$DISPLAY" -rfbport 5901 -localhost \
  -PasswordFile "$HOME/.vnc/passwd-apple" \
  -SecurityTypes VncAuth -AppleClipboard
```

On the Mac, tunnel to the server:

```sh
ssh -N -L 5901:127.0.0.1:5901 your-user@your-oracle-linux-host
```

In Screen Sharing, connect to `localhost:5901`, enter the VNC password, and enable
**Edit → Use Shared Clipboard**. Copy text in a Mac application and paste into
the remote application; copy text in the remote application and paste on the Mac.
Linux applications generally use Ctrl+C/Ctrl+V; the Mac uses Command+C/Command+V.

This compatibility mode uses VNC password authentication. Its protocol does not
encrypt desktop or clipboard traffic; the localhost binding and SSH tunnel above
provide the protected transport. It does not implement Apple's account/SRP
authentication. Existing TigerVNC authentication settings are never overridden:
Apple connections fail if `VncAuth` is not enabled.

## Scope and checks

- Supports text, including non-ASCII UTF-8, in both directions. It does not transfer
  files, images, rich text, or file promises.
- Retains `AcceptCutText`, `SendCutText`, and read-only client access restrictions.
- Enforces `MaxCutText` against incoming expanded archives, plus a hard 7 MiB
  compressed/expanded limit. Malformed lengths, truncated archives, invalid UTF-8
  and inconsistent zlib sizes terminate that client connection.
- Ordinary RFB clients continue using the existing clipboard protocol.
- The tests cover codecs, byte-by-byte fragmentation, standard and Apple password
  handshakes, shared-session flags, clipboard access controls, and manual transfers.

Verified locally on macOS 26.7: the compiled TigerVNC test peer exchanged
`Mac clipboard test café 日本語` and `Linux clipboard test café 日本語` with the
built-in Screen Sharing app. This tests the actual server protocol library, using
a synthetic framebuffer. Separately, all three Oracle Linux versions passed
package builds and bidirectional X11 clipboard tests with Xvfb/xclip, including
Unicode and newlines. [Passing build and artifacts](https://github.com/osheinin/LinuxScreenSharing/actions/runs/36196892008).
Full GNOME desktop sessions and other macOS versions still need acceptance testing.
See `docs/apple-clipboard.md` for protocol details and the remaining matrix.

To repeat the native-client test after a normal CMake build, run
`build/tests/apple/apple_clipboard_peer`, connect to `localhost:5992` using the
test password `probe`, and copy the Mac test string above. The peer prints PASS
only for that exact string and offers the Linux test string in response. It binds
only loopback and exposes no real desktop. Stop it with Ctrl+C when finished.

Source projects and platform references:
[TigerVNC](https://github.com/TigerVNC/tigervnc),
[Oracle Software Collections](https://docs.oracle.com/en/operating-systems/oracle-linux/scl-user/),
[Oracle repository documentation](https://docs.oracle.com/en/operating-systems/oracle-linux/software-management/sfw-mgmt-AvailableYumRepositories.html).
