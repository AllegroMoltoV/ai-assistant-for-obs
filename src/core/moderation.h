#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aia {

// 送る前の検査に使うモデル。https://developers.openai.com/api/docs/guides/moderation
inline constexpr const char *kModerationModel = "omni-moderation-latest";

// 検査で問題ありとされた内容の代わりに、モデルへ送る文
inline constexpr const char *kWithheldUserText =
	"[The plugin did not send this message because the moderation check flagged it.]";
inline constexpr const char *kWithheldToolOutput =
	R"({"status":"withheld","reason":"The plugin did not send this tool result because the moderation check flagged it."})";

nlohmann::json build_moderation_request(const std::vector<std::string> &texts);

struct ModerationResult {
	bool ok = false;
	// 送った文と同じ順。2026-11-12 に 3 件の配列を送り、同じ順で 3 件が返ることを確かめた
	std::vector<bool> flagged;
};

// 結果の数が送った数と違うときも ok を偽にする。検査できなかった内容は送らない
ModerationResult parse_moderation(long http_status, const nlohmann::json &body, size_t expected_count);

} // namespace aia
