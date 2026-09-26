// SPDX-License-Identifier: GPL-2.0-or-later
// Manual interop test: loopback only, synthetic pixels, no real desktop.
#include <rfb/SConnection.h>
#include <rfb/SMsgWriter.h>
#include <rfb/PixelFormat.h>
#include <rfb/ServerCore.h>
#include <rfb/SSecurityVncAuth.h>
#include <rfb/obfuscate.h>
#include <core/Configuration.h>
#include <core/Timer.h>
#include <rdr/FdInStream.h>
#include <rdr/FdOutStream.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <cstring>

class Peer : public rfb::SConnection {
public:
  Peer() : SConnection(rfb::AccessDefault), drawn(false) {}
  void keyEvent(uint32_t, uint32_t, bool) override {}
  void pointerEvent(const core::Point&, uint16_t) override {}
  void setDesktopSize(int, int, const rfb::ScreenSet&) override {}
  void clientReady(bool shared) override {
    client.setDimensions(640, 480);
    client.setName("TigerVNC native clipboard test");
    client.setPF(rfb::PixelFormat(32, 24, false, true, 255, 255, 255, 16, 8, 0));
    std::cout << "Authenticated; shared=" << shared << std::endl;
    desktopReady();
    announceClipboard(true);
  }
  void framebufferUpdateRequest(const core::Rect&, bool incremental) override {
    if (drawn && incremental) return;
    auto* out = getOutStream();
    out->writeU8(0); out->pad(1); out->writeU16(1);
    out->writeU16(0); out->writeU16(0);
    out->writeU16(640); out->writeU16(480); out->writeU32(0);
    std::vector<uint8_t> pixels(640 * 480 * client.pf().bpp / 8, 96);
    out->writeBytes(pixels.data(), pixels.size()); out->flush();
    drawn = true;
  }
  void handleClipboardRequest() override {
    sendClipboardData("Linux clipboard test caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    std::cout << "Sent synthetic UTF-8 server clipboard" << std::endl;
  }
  void handleClipboardAnnounce(bool available) override {
    std::cout << "Client clipboard text available=" << available << std::endl;
    if (available) requestClipboard();
  }
  void handleClipboardData(const char* text) override {
    const char* expected = "Mac clipboard test caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    std::cout << "Mac -> TigerVNC Unicode fixture: "
              << (!strcmp(text, expected) ? "PASS" : "different clipboard (not logged)")
              << std::endl;
    if (!strcmp(text, expected)) announceClipboard(true);
  }
private:
  bool drawn;
};

int main()
{
  signal(SIGPIPE, SIG_IGN);
  rfb::Server::appleClipboard.setParam(true);
  core::Configuration::setParam("SecurityTypes", "VncAuth");
  std::vector<uint8_t> password = rfb::obfuscate("probe");
  std::string hex;
  for (uint8_t byte : password) {
    hex += "0123456789abcdef"[byte >> 4];
    hex += "0123456789abcdef"[byte & 15];
  }
  core::Configuration::setParam("Password", hex.c_str());
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address = {};
  address.sin_family = AF_INET; address.sin_port = htons(5992);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
      listen(listener, 1)) { perror("listen"); return 1; }
  std::cout << "Synthetic test only: localhost:5992, password probe" << std::endl;
  for (;;) {
    int fd = accept(listener, nullptr, nullptr);
    if (fd < 0) { perror("accept"); return 1; }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    try {
      rdr::FdInStream in(fd); rdr::FdOutStream out(fd);
      Peer peer;
      peer.setStreams(&in, &out); peer.initialiseProtocol();
      while (peer.state() != rfb::SConnection::RFBSTATE_CLOSING) {
        while (peer.processMsg()) {}
        out.flush();
        core::Timer::checkTimeouts();
        fd_set readfds, writefds;
        FD_ZERO(&readfds); FD_SET(fd, &readfds);
        FD_ZERO(&writefds);
        if (out.hasBufferedData()) FD_SET(fd, &writefds);
        timeval timeout = {0, 100000};
        select(fd + 1, &readfds, &writefds, nullptr, &timeout);
      }
    } catch (const std::exception& e) {
      std::cout << "Connection ended: " << e.what() << std::endl;
    }
    close(fd);
  }
}
