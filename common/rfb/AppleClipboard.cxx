// SPDX-License-Identifier: GPL-2.0-or-later
#include <rfb/AppleClipboard.h>
#include <rfb/Exception.h>
#include <core/string.h>
#include <cstring>
#include <zlib.h>

namespace {
bool validUTF8(const std::string& s)
{
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i++];
    if (c < 0x80) { if (!c) return false; continue; }
    unsigned n;
    uint32_t value, minimum;
    if (c >= 0xc2 && c <= 0xdf) { n = 1; value = c & 31; minimum = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; value = c & 15; minimum = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; value = c & 7; minimum = 0x10000; }
    else return false;
    if (s.size() - i < n) return false;
    while (n--) {
      uint8_t next = s[i++];
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 63);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}
void put32(std::vector<uint8_t>& out, uint32_t n)
{
  out.push_back(n >> 24); out.push_back(n >> 16);
  out.push_back(n >> 8); out.push_back(n);
}
void putString(std::vector<uint8_t>& out, const std::string& str)
{
  put32(out, str.size());
  out.insert(out.end(), str.begin(), str.end());
}
class ArchiveReader {
public:
  ArchiveReader(const uint8_t* data, size_t size) : ptr(data), left(size) {}
  uint32_t integer() {
    if (left < 4) throw rfb::protocol_error("Truncated Apple pasteboard");
    uint32_t n = (uint32_t(ptr[0]) << 24) | (uint32_t(ptr[1]) << 16) |
                 (uint32_t(ptr[2]) << 8) | ptr[3];
    ptr += 4; left -= 4; return n;
  }
  std::string string() {
    size_t n = integer();
    if (n > left) throw rfb::protocol_error("Invalid Apple pasteboard length");
    std::string str(reinterpret_cast<const char*>(ptr), n);
    ptr += n; left -= n; return str;
  }
  const uint8_t* ptr;
  size_t left;
};
}

std::vector<uint8_t> rfb::packAppleClipboard(const char* text, bool promise)
{
  if (!text) return {};
  size_t len = strlen(text);
  if (len > appleClipboardLimit - 1024 || !validUTF8(text))
    throw std::invalid_argument("Invalid or oversized Apple clipboard text");
  std::vector<uint8_t> out;
  put32(out, 1);
  putString(out, "public.utf8-plain-text");
  put32(out, 0);
  put32(out, 3);
  putString(out, "com.apple.ostype"); putString(out, "utf8");
  putString(out, "com.apple.nspboard-type"); putString(out, "NSStringPboardType");
  putString(out, "public.mime-type"); putString(out, "text/plain;charset=utf-8");
  putString(out, promise ? "" : text);
  return out;
}

bool rfb::unpackAppleClipboard(const uint8_t* data, size_t size,
                               bool promise, std::string* text)
{
  text->clear();
  if (!size) return false;
  ArchiveReader in(data, size);
  uint32_t count = in.integer();
  if (count > in.left / 16)
    throw protocol_error("Invalid Apple pasteboard item count");
  bool found = false;
  for (uint32_t i = 0; i < count; ++i) {
    std::string type = in.string();
    in.integer(); // reserved
    uint32_t aliases = in.integer();
    if (aliases > in.left / 8)
      throw protocol_error("Invalid Apple pasteboard alias count");
    for (uint32_t j = 0; j < aliases; ++j) {
      in.string(); in.string();
    }
    std::string value = in.string();
    if (type == "public.utf8-plain-text" && !found) {
      if (!promise && !validUTF8(value))
        throw protocol_error("Invalid UTF-8 Apple clipboard text");
      *text = promise ? "" : core::convertLF(value.data(), value.size());
      found = true;
    }
  }
  if (in.left) throw protocol_error("Trailing Apple pasteboard data");
  return found;
}

std::vector<uint8_t> rfb::inflateAppleClipboard(const uint8_t* data, size_t size,
                                              size_t expanded, size_t limit)
{
  if (expanded > limit || expanded > appleClipboardLimit ||
      size > appleClipboardLimit || size < 6)
    throw protocol_error("Apple clipboard exceeds size limit");
  // One extra byte detects a forged expansion length without unbounded output.
  std::vector<uint8_t> out(expanded + 1);
  z_stream z = {};
  if (inflateInit(&z) != Z_OK) throw std::runtime_error("inflateInit failed");
  z.next_in = const_cast<Bytef*>(data); z.avail_in = size;
  z.next_out = out.data(); z.avail_out = out.size();
  int result = inflate(&z, Z_SYNC_FLUSH);
  bool ok = (result == Z_OK || result == Z_STREAM_END) &&
            z.total_out == expanded && z.avail_in == 0;
  // Apple's stream ends at a sync-flush boundary, without Z_STREAM_END.
  if (result == Z_OK)
    ok = ok && size >= 4 && memcmp(data + size - 4, "\0\0\xff\xff", 4) == 0;
  inflateEnd(&z);
  if (!ok) throw protocol_error("Invalid compressed Apple clipboard");
  out.resize(expanded);
  return out;
}

std::vector<uint8_t> rfb::deflateAppleClipboard(const std::vector<uint8_t>& data)
{
  if (data.size() > appleClipboardLimit)
    throw std::invalid_argument("Apple clipboard exceeds size limit");
  std::vector<uint8_t> out(compressBound(data.size()) + 16);
  z_stream z = {};
  if (deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK)
    throw std::runtime_error("deflateInit failed");
  z.next_in = const_cast<Bytef*>(data.data()); z.avail_in = data.size();
  z.next_out = out.data(); z.avail_out = out.size();
  int result = deflate(&z, Z_SYNC_FLUSH);
  bool ok = result == Z_OK && z.avail_in == 0 && z.avail_out != 0;
  size_t length = z.total_out;
  deflateEnd(&z);
  if (!ok) throw std::runtime_error("Apple clipboard compression failed");
  out.resize(length);
  return out;
}
