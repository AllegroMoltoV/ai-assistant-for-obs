#include "tool-output.h"

namespace aia {

namespace {

constexpr const char *kTruncatedHead = "\n[truncated: the rest was omitted]";
constexpr const char *kTruncatedTail = "[truncated: earlier lines were omitted]\n";

bool is_continuation(unsigned char c)
{
	return (c & 0xC0) == 0x80;
}

} // namespace

std::string limit_head(const std::string &text, size_t max_bytes)
{
	if (text.size() <= max_bytes)
		return text;
	size_t end = max_bytes;
	while (end > 0 && is_continuation(static_cast<unsigned char>(text[end])))
		--end;
	return text.substr(0, end) + kTruncatedHead;
}

std::string limit_tail(const std::string &text, size_t max_bytes)
{
	if (text.size() <= max_bytes)
		return text;
	size_t start = text.size() - max_bytes;
	while (start < text.size() && is_continuation(static_cast<unsigned char>(text[start])))
		++start;
	// 行の途中から始めないよう、次の改行の後ろから残す
	const size_t newline = text.find('\n', start);
	if (newline != std::string::npos && newline + 1 < text.size())
		start = newline + 1;
	return kTruncatedTail + text.substr(start);
}

} // namespace aia
