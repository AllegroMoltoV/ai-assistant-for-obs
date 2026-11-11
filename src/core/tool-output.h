#pragma once

#include <cstddef>
#include <string>

namespace aia {

// 道具の結果をモデルに渡す量の上限。約 8,000 トークンを、日本語 1 文字 3 バイト・1 トークン前後として見積もった値。
// 親課題の design の仮決めで、子課題 .3 の試験で見直す
inline constexpr size_t kToolOutputMaxBytes = 24000;

// 上限を超えたら先頭を残し、切ったことを末尾に書く。UTF-8 の文字の途中では切らない
std::string limit_head(const std::string &text, size_t max_bytes = kToolOutputMaxBytes);

// 上限を超えたら末尾を残し、切ったことを先頭に書く。ログのように新しい内容が末尾にあるもので使う
std::string limit_tail(const std::string &text, size_t max_bytes = kToolOutputMaxBytes);

} // namespace aia
