#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace aia {

// AI に送る前に秘密の値を伏せる。伏せた値はこの文字列に置き換える
inline constexpr const char *kMaskedValue = "***";

// 文字列の中の秘密の値を伏せる。対象は次の 3 つ。
// - URL の問い合わせ文字列の値 (トークンが入ることが多いため、名前によらずすべて)
// - 配信の URL (rtmp・rtmps・srt・rist) のアプリ名より後ろの経路 (配信キーが入るため)
// - OpenAI の API キーや Bearer のトークンのような、形で分かるキー
std::string mask_secrets_in_text(const std::string &text);

// JSON の中の秘密の値を伏せる。項目名の最後の語が key・token・password などの値は丸ごと伏せ、
// それ以外の文字列の値には mask_secrets_in_text をかける
nlohmann::json mask_secrets_in_json(const nlohmann::json &value);

// 項目名が秘密の値を表すとき真。例: stream_key、bearer_token、password。keyint_sec や key_color_type は偽
bool is_secret_field_name(const std::string &name);

} // namespace aia
