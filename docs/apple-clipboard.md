# Apple clipboard interoperability notes

The implementation is an independent protocol implementation. No Apple code is
linked or distributed. Wire behavior was verified against Screen Sharing.app on
macOS 26.7 (25G229). An experimental public description provided initial leads:
https://github.com/renegadelink/iShareScreen/blob/main/docs/apple_vnc_rfc.md.
The deployed subset was checked with a loopback peer and the compiled TigerVNC
library rather than assuming that description was complete.

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

## Validation matrix

| Check | Result |
|---|---|
| Core compilation on macOS arm64 | Passed |
| Codec and protocol tests, including ASan/UBSan | Passed |
| Native Mac → compiled server, accented/Japanese UTF-8 | Passed |
| Compiled server → native Mac paste, accented/Japanese UTF-8 | Passed |
| Oracle Linux 7/8/9 x86_64 package builds | Passed in CI |
| Xvfb/xclip clipboard on Oracle Linux 7/8/9, UTF-8/newlines both directions | Passed in CI |
| Native Mac connected to full GNOME sessions on Oracle Linux 7/8/9 | Pending deployment acceptance |
| Other macOS versions / Wayland / rich clipboard formats | Not validated |

For release acceptance, on each Oracle Linux version run the packaged server
against a real X11 session. Verify both clipboard directions in a terminal and a
GUI editor; empty text, newlines and non-ASCII text; repeated copy/paste; reconnect;
`AcceptCutText=0`; `SendCutText=0`; read-only VNC credentials; and an ordinary
TigerVNC viewer alongside the Mac client. Do not infer desktop compatibility from
the synthetic peer alone.

The verified implementation is commit `75b0256c2865cedbd4246d6a6979643ba5268e56`:
[Oracle Linux build/test results and packages](https://github.com/osheinin/LinuxScreenSharing/actions/runs/36196892008).
The inherited Linux/macOS/Windows build and test jobs also passed at that commit.
