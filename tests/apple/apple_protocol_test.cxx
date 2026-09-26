// SPDX-License-Identifier: GPL-2.0-or-later
#include <rfb/SConnection.h>
#include <rfb/SMsgReader.h>
#include <rfb/SMsgWriter.h>
#include <rfb/ServerCore.h>
#include <rfb/SSecurityVncAuth.h>
#include <rfb/PixelFormat.h>
#include <rfb/Exception.h>
#include <rfb/obfuscate.h>
#include <core/string.h>
#include <rdr/MemOutStream.h>
#include <rdr/InStream.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <iostream>
extern "C" {
#include <rfb/d3des.h>
}

class Input : public rdr::InStream {
public:
  Input() { ptr = end = buffer; }
  void append(const uint8_t* data, size_t size) {
    assert(size <= sizeof(buffer) - (end - buffer));
    memcpy(const_cast<uint8_t*>(end), data, size); end += size;
  }
  size_t pos() override { return ptr - buffer; }
private:
  bool overrun(size_t) override { return false; }
  uint8_t buffer[65536];
};

class Connection : public rfb::SConnection {
public:
  Connection() : SConnection(rfb::AccessDefault), announces(0), requests(0),
                 transfers(0), available(false), shared(false) {}
  void normal(Input* in, rdr::MemOutStream* out, bool apple = true) {
    setStreams(in, out); client.apple = apple;
    setReader(new rfb::SMsgReader(this, in));
    setWriter(new rfb::SMsgWriter(&client, out));
    setState(RFBSTATE_NORMAL);
  }
  void clientReady(bool value) override {
    shared = value;
    client.setDimensions(640, 480); client.setName("Test");
    desktopReady();
  }
  void keyEvent(uint32_t, uint32_t, bool) override {}
  void pointerEvent(const core::Point&, uint16_t) override {}
  void setDesktopSize(int, int, const rfb::ScreenSet&) override {}
  void handleClipboardAnnounce(bool value) override { ++announces; available = value; }
  void handleClipboardRequest() override { ++requests; }
  void handleClipboardData(const char* text) override { ++transfers; received = text; }
  int announces, requests, transfers;
  bool available, shared;
  std::string received;
};

static void handshake(int minor, bool enabled, bool correctPassword = true)

{
  const bool apple = minor == 889;
  rfb::Server::appleClipboard.setParam(enabled);
  core::Configuration::setParam("SecurityTypes", "VncAuth");
  auto password = rfb::obfuscate("probe");
  core::Configuration::setParam("Password", core::binToHex(password.data(), password.size()).c_str());
  Input input; rdr::MemOutStream output;
  Connection c; c.setStreams(&input, &output); c.initialiseProtocol();
  assert(!memcmp(output.data(), enabled ? "RFB 003.889\n" : "RFB 003.008\n", 12));
  char version[13];
  snprintf(version, sizeof(version), "RFB 003.%03d\n", minor);
  input.append(reinterpret_cast<const uint8_t*>(version), 12);
  output.clear(); assert(c.processMsg());
  assert(output.length() == (minor == 3 ? 4 : 2));
  output.clear();
  if (!apple && minor != 3) {
    const uint8_t selection = 2; // Standard 3.7/3.8 select VncAuth explicitly.
    input.append(&selection, 1);
  }
  if (!apple && minor != 3)
    assert(c.processMsg());
  assert(!c.processMsg());
  assert(output.length() == 16);
  uint8_t response[16], key[8] = {'p','r','o','b','e',0,0,0};
  deskey(key, EN0);
  des(const_cast<uint8_t*>(output.data()), response);
  des(const_cast<uint8_t*>(output.data()) + 8, response + 8);
  if (!correctPassword)
    response[0] ^= 1;
  input.append(response, 16); output.clear();
  if (!correctPassword) {
    // Failed authentication enters the delayed failure state, never normal.
    c.processMsg();
    assert(c.state() == rfb::SConnection::RFBSTATE_SECURITY_FAILURE);
    return;
  }
  assert(c.processMsg());
  assert(output.length() == 4 && output.data()[3] == 0);
  uint8_t init = apple ? 0xc0 : 0;
  input.append(&init, 1); output.clear(); c.processMsg();
  assert(c.state() == rfb::SConnection::RFBSTATE_NORMAL);
  assert(!c.shared); // Apple's high ClientInit bits must not force sharing.
  assert(output.length() == (apple ? 50 : 28));
  assert(output.data()[23] == (apple ? 26 : 4));
  if (apple) {
    assert(output.data()[30] & 0x80); // command zero
    assert(output.data()[33] & 1); // command 31
  }
}

int main()
{
  for (int minor : {3, 7, 8, 889}) {
    handshake(minor, true);
    handshake(minor, true, false);
    if (minor != 889)
      handshake(minor, false);
  }

  rfb::ClientParams client;
  client.apple = true;
  rdr::MemOutStream packet;
  rfb::SMsgWriter writer(&client, &packet);
  writer.writeAppleClipboard("caf\xc3\xa9", true);
  Input input; rdr::MemOutStream output;
  Connection c; c.normal(&input, &output);
  for (size_t i = 0; i < packet.length(); ++i) {
    input.append(packet.data() + i, 1);
    assert(c.processMsg() == (i + 1 == packet.length()));
  }
  assert(c.announces == 1 && c.available && c.transfers == 0);
  c.requestClipboard();
  const uint8_t fetch[] = {20,0,0,4,0,1,0,3};
  assert(output.length() == sizeof(fetch) && !memcmp(output.data(), fetch, sizeof(fetch)));
  packet.clear(); writer.writeAppleClipboard("caf\xc3\xa9", false);
  input.append(packet.data(), packet.length()); assert(c.processMsg());
  assert(c.transfers == 1 && c.received == "caf\xc3\xa9");

  Input manualInput; rdr::MemOutStream manualOutput;
  Connection manual; manual.normal(&manualInput, &manualOutput);
  manualInput.append(packet.data(), packet.length()); assert(manual.processMsg());
  assert(manual.announces == 1 && manual.available && manual.transfers == 0);
  manual.requestClipboard();
  assert(manual.transfers == 1 && manual.received == "caf\xc3\xa9");
  assert(manualOutput.length() == 0); // The full manual clipboard is cached.

  // Clipboard access and the administrator's direction switches apply to Apple too.
  c.setAccessRights(rfb::AccessView);
  input.append(packet.data(), packet.length()); assert(c.processMsg());
  assert(c.transfers == 1);
  output.clear(); c.requestClipboard(); c.announceClipboard(true);
  c.sendClipboardData("denied"); assert(output.length() == 0);
  c.setAccessRights(rfb::AccessDefault);
  rfb::Server::acceptCutText.setParam(false);
  input.append(packet.data(), packet.length()); assert(c.processMsg());
  assert(c.transfers == 1);
  rfb::Server::acceptCutText.setParam(true);
  rfb::Server::sendCutText.setParam(false);
  const uint8_t request[] = {11,0,0,0,0,0,0,0};
  input.append(request, sizeof(request)); assert(c.processMsg());
  assert(c.requests == 0 && output.length() == 0);
  rfb::Server::sendCutText.setParam(true);

  // Disabling automatic sharing suppresses notifications, while enabling it
  // announces an existing local selection once.
  Input toggleInput; rdr::MemOutStream toggleOutput;
  Connection toggle; toggle.normal(&toggleInput, &toggleOutput);
  toggle.announceClipboard(true);
  assert(toggleOutput.length() == 0);
  const uint8_t enableSharing[] = {21, 0, 0, 1, 0, 0, 0, 0};
  toggleInput.append(enableSharing, sizeof(enableSharing));
  assert(toggle.processMsg());
  assert(toggleOutput.length() == 8);
  toggleOutput.clear();
  const uint8_t disableSharing[] = {21, 0, 0, 2, 0, 0, 0, 0};
  toggleInput.append(disableSharing, sizeof(disableSharing));
  assert(toggle.processMsg());
  toggle.announceClipboard(true);
  assert(toggleOutput.length() == 0);

  // Validate the wire header before allocating or accepting a payload.
  auto rejectsPacket = [](const std::vector<uint8_t>& bytes) {
    Input malformedInput; rdr::MemOutStream malformedOutput;
    Connection malformed; malformed.normal(&malformedInput, &malformedOutput);
    malformedInput.append(bytes.data(), bytes.size());
    bool failed = false;
    try { malformed.processMsg(); }
    catch (const rfb::protocol_error&) { failed = true; }
    assert(failed);
    assert(malformed.transfers == 0 && malformed.announces == 0);
  };
  std::vector<uint8_t> invalid(packet.data(), packet.data() + packet.length());
  invalid[2] = 2; // Only promise flags 0 and 1 are defined.
  rejectsPacket(invalid);
  invalid.assign(packet.data(), packet.data() + packet.length());
  invalid[8] = 0xff; // Forged expanded length; rejected without allocating it.
  rejectsPacket(invalid);
  invalid.assign(packet.data(), packet.data() + packet.length());
  invalid[12] = 0xff; // Forged compressed length.
  rejectsPacket(invalid);
  invalid.assign(packet.data(), packet.data() + packet.length());
  core::Configuration::setParam("MaxCutText", "1");
  rejectsPacket(invalid);
  core::Configuration::setParam("MaxCutText", "262144");

  // Proprietary opcodes are never accepted on ordinary RFB connections.
  Input standardInput; rdr::MemOutStream standardOutput;
  Connection standard; standard.normal(&standardInput, &standardOutput, false);
  standardInput.append(packet.data(), packet.length());
  bool rejected = false;
  try { standard.processMsg(); } catch (const std::exception&) { rejected = true; }
  assert(rejected);

  // An Apple connection cannot silently enable VncAuth if disabled by the admin.
  rfb::Server::appleClipboard.setParam(true);
  core::Configuration::setParam("SecurityTypes", "None");
  Input deniedInput; rdr::MemOutStream deniedOutput;
  Connection denied; denied.setStreams(&deniedInput, &deniedOutput);
  denied.initialiseProtocol();
  deniedInput.append(reinterpret_cast<const uint8_t*>("RFB 003.889\n"), 12);
  rejected = false;
  try { denied.processMsg(); } catch (const std::exception&) { rejected = true; }
  assert(rejected);
  std::cout << "Apple protocol, fragmentation, authentication and permission tests passed" << std::endl;
}
