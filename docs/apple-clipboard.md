# Apple clipboard interoperability notes

The implementation adds optional UTF-8 plain-text clipboard interoperability
with macOS Screen Sharing to the shared TigerVNC server protocol library.
Enable it with `-AppleClipboard -SecurityTypes VncAuth`. The option is off by
default. Use a localhost listener and SSH tunnel: VncAuth does not encrypt the
session. File transfer, rich text, Apple account authentication and Apple's
high-performance screen-sharing transport are outside this implementation.

## Implementation provenance

This is newly written interoperability code; no Apple source code or binaries
are incorporated, linked or redistributed. It is not a clean-room implementation.
Development included reading public protocol notes, inspecting disassembly of
the proprietary Apple ScreenSharing framework, and observing exchanges with the
native client on macOS 26.7 (25G229). The public notes used for initial leads were:
https://github.com/renegadelink/iShareScreen/blob/main/docs/apple_vnc_rfc.md.
They were not treated as an authoritative or complete specification.

This provenance needs explicit maintainer review and any appropriate legal
assessment before adoption. It is not a claim that Apple's license or applicable
reverse-engineering rules have been cleared. No disassembly is included here.
The implementation and preparation used AI assistance; a human contributor must
review and take responsibility for the submission and ongoing maintenance.

## Negotiation

`AppleClipboard` advertises `RFB 003.889`. Apple replies with 3.889; ordinary
clients can choose the existing 3.3/3.7/3.8 protocols. The Apple password branch
receives a one-byte security count and type 2, immediately followed by the usual
16-byte VNC challenge. It sends **no security-selection byte**. Password checking
is still performed by TigerVNC's normal VncAuth implementation.

Only a negotiated Apple connection receives the 22-byte desktop-name prefix:
`u16 zero, u32 flags=0, 16-byte MSB-first client-command bitmap`. This enables the
clipboard UI. The low bit of ClientInit is the sharing flag; Apple's other bits
must not turn an exclusive request into a shared request.

The client also sends type 10 (control mode) and type 9 (automatic framebuffer
updates). The latter uses the existing continuous update path with output buffer
backpressure. The standard continuous-update acknowledgement is not sent to Apple.

## Clipboard messages

All integers below are big-endian.

| Direction | Type | Body after type byte |
|---|---:|---|
| Client → server | 21 | two padding bytes, selector 1=enable/2=disable, four padding bytes |
| Client → server | 11 | promise flag, six padding bytes |
| Both | 31 | pad, promise flag, pad, reserved u32, expanded u32, compressed u32, zlib bytes |
| Server → client | 20 | pad, u16 4, u16 1, u16 command (2=changed, 3=request client data) |

Each archive has an independent zlib stream; Apple's Z_SYNC_FLUSH termination is
accepted, as is a complete zlib stream. Archives contain a u32 flavor count.
Each flavor has a length-prefixed UTI, a reserved u32, a u32 alias count,
length-prefixed alias name/value pairs, and length-prefixed data. An empty expanded
archive means no flavors. Only `public.utf8-plain-text` is consumed and exported.

Client promises announce available text without transferring it. TigerVNC requests
the data when its desktop needs it. In the reverse direction, TigerVNC announces
changes; the Mac requests promises and then data when a local paste needs it.
x0vncserver fetches advertised client text immediately so it can own the X11
selection. Other desktop implementations may defer the request until a paste.

## Reproducing validation

After a normal CMake build, run `ctest --test-dir build -R '^apple_'`.
These tests cover archive parsing, compression limits, authentication, standard
RFB negotiation with the option on and off, fragmentation and access controls.

On Linux, `tests/apple/x11_clipboard_smoke.py build` exercises x0vncserver
against Xvfb. `tests/apple/xvnc_clipboard_smoke.py build` exercises a separately
built Xvnc and the actual TigerVNC viewer. Both require Xvfb, xclip, Python 3 and
OpenSSL; the latter also uses xdotool. Run them in an isolated test environment.
The Apple clipboard CI workflow contains an Xorg-based Xvnc build recipe.

The optional `build/tests/apple/apple_clipboard_peer` is a loopback-only native
client test fixture with password `probe`. It exposes a synthetic framebuffer,
not a user desktop. Connect Screen Sharing to `localhost:5992`, enable shared
clipboard, copy `Mac clipboard test café 日本語` to the server, then paste the
server's `Linux clipboard test café 日本語` on the Mac. Stop the peer afterward.

Before release acceptance, test a real Xvnc desktop with native Screen Sharing
and TigerVNC Viewer, on supported macOS versions. Exercise both directions in a
terminal and GUI editor, empty text, newlines, Unicode, repeated updates,
reconnect, shared-clipboard off/on, `AcceptCutText=0`, `SendCutText=0`, read-only
credentials and simultaneous clients. Synthetic protocol and X11 tests do not
establish GNOME desktop or native-client UI compatibility.
