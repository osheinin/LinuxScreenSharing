// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef RFB_APPLECLIPBOARD_H
#define RFB_APPLECLIPBOARD_H

#include <stdint.h>
#include <string>
#include <vector>

namespace rfb {
  // Apple Screen Sharing's packed pasteboard. Only plain text is exported;
  // file promises and arbitrary property lists are never interpreted.
  const size_t appleClipboardLimit = 7 * 1024 * 1024;
  // Leave room for flavor metadata and worst-case deflate overhead.
  const size_t appleClipboardTextLimit = appleClipboardLimit - 16 * 1024;
  std::vector<uint8_t> packAppleClipboard(const char* text, bool promise);
  bool unpackAppleClipboard(const uint8_t* data, size_t size,
                            bool promise, std::string* text);
  std::vector<uint8_t> inflateAppleClipboard(const uint8_t* data, size_t size,
                                           size_t expanded, size_t limit);
  std::vector<uint8_t> deflateAppleClipboard(const std::vector<uint8_t>& data);
}
#endif
