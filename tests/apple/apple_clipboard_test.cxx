// SPDX-License-Identifier: GPL-2.0-or-later
#include <rfb/AppleClipboard.h>
#include <cassert>
#include <iostream>
#include <functional>

static void rejects(const std::function<void()>& action)
{
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  static int test = 0;
  ++test;
  if (!rejected) std::cerr << "Expected rejection at case " << test << std::endl;
  assert(rejected);
}

int main()
{
  const char* samples[] = {"", "plain text", "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "line1\nline2"};
  for (auto text : samples) {
    auto archive = rfb::packAppleClipboard(text, false);
    auto compressed = rfb::deflateAppleClipboard(archive);
    auto decoded = rfb::inflateAppleClipboard(compressed.data(), compressed.size(), archive.size(), 262144);
    std::string result;
    assert(rfb::unpackAppleClipboard(decoded.data(), decoded.size(), false, &result));
    assert(result == text);
    for (size_t i = 1; i < archive.size(); ++i)
      rejects([&] { rfb::unpackAppleClipboard(archive.data(), i, false, &result); });
    rejects([&] { rfb::inflateAppleClipboard(compressed.data(), compressed.size(), archive.size() - 1, 262144); });
    rejects([&] { rfb::inflateAppleClipboard(compressed.data(), compressed.size(), archive.size() + 1, 262144); });
    rejects([&] { rfb::inflateAppleClipboard(compressed.data(), compressed.size() - 1, archive.size(), 262144); });
    rejects([&] { rfb::inflateAppleClipboard(compressed.data(), compressed.size(), archive.size(), 1); });
    archive[0] = 255;
    rejects([&] { rfb::unpackAppleClipboard(archive.data(), archive.size(), false, &result); });
  }
  auto empty = rfb::deflateAppleClipboard({});
  assert(rfb::inflateAppleClipboard(empty.data(), empty.size(), 0, 0).empty());
  rejects([] { rfb::packAppleClipboard("\xc0\xaf", false); });
  for (const char* invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"})
    rejects([&] { rfb::packAppleClipboard(invalid, false); });
  auto nul = rfb::packAppleClipboard("a", false);
  nul.back() = 0;
  std::string result;
  rejects([&] { rfb::unpackAppleClipboard(nul.data(), nul.size(), false, &result); });
  auto trailing = rfb::packAppleClipboard("a", false);
  trailing.push_back(0);
  rejects([&] { rfb::unpackAppleClipboard(trailing.data(), trailing.size(), false, &result); });
  std::string oversized(rfb::appleClipboardTextLimit + 1, 'a');
  rejects([&] { rfb::packAppleClipboard(oversized.c_str(), false); });
  std::cout << "Apple clipboard codec tests passed" << std::endl;
}
