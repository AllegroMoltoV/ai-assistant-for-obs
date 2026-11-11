#pragma once

#include <string>
#include <vector>

namespace aia {

// エージェントが追加できる種類。依頼者の決定で、この 6 つ以外は追加しない
enum class AddKind { NoiseSuppression, Gain, Compressor, Crop, ApplicationAudioCapture, BrowserSource };

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

} // namespace aia
