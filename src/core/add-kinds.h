#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aia {

// エージェントが追加できる種類。依頼者の決定で、この 7 つ以外は追加しない (色ソースは 2026-11-12 に追加)
enum class AddKind { NoiseSuppression, Gain, Compressor, Crop, ApplicationAudioCapture, BrowserSource, ColorSource };

struct AddKindInfo {
	AddKind kind;
	// 道具の引数で使う名前
	const char *name;
	// obs_source_create に渡す ID。ノイズ抑制は libobs が版を付けた ID を使う
	const char *obs_id;
	// フィルターならソースに付け、そうでなければシーンに置く
	bool is_filter;
};

const std::vector<AddKindInfo> &add_kinds();

// 名前に一致する種類を返す。無ければ nullptr
const AddKindInfo *find_add_kind(const std::string &name);

// AI が追加したことを示す目印。追加したときに、追加したソースやフィルタの private settings に書く。
// private settings はシーンコレクションとともに保存され、再起動後も残る
// (OBS 33.0.1 の libobs/obs.c の obs_save_source と obs_load_source_type)。
// ソースとフィルタの複製は private settings も写す (libobs/obs-source.c の obs_source_duplicate) ため、目印には追加したときの名前と、
// フィルタなら付け先のソースの名前も入れ、名前が一致するものだけを AI が追加したものとして扱う
inline constexpr const char *kAddedMarkKey = "ai_assistant_for_obs_added";
inline constexpr const char *kAddedMarkNameKey = "ai_assistant_for_obs_added_name";
inline constexpr const char *kAddedMarkParentKey = "ai_assistant_for_obs_added_parent";

// 書き込む目印。parent はフィルタの付け先のソースの名前で、ソースでは空
nlohmann::json added_mark(const std::string &name, const std::string &parent);

// private settings に、この名前と付け先で書いた目印があるとき真。目印の無いものは削除できない
bool has_added_mark(const nlohmann::json &private_settings, const std::string &name, const std::string &parent);

} // namespace aia
