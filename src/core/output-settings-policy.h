#pragma once

#include <string>

namespace aia {

enum class OutputMode { Simple, Advanced };

enum class OutputSettingClass {
	// 書き換えてよい。反映は次の開始時か obs_frontend_reset_video()
	Allowed,
	// 公開の手段で反映できないため、書き換えずに設定画面での手順を案内する
	GuideOnly,
};

// 出力と映像の設定の項目の扱いを決める。根拠は docs/reports/2026-11-11-output-settings-apply.md の考察の表。
// key は "Section/Key" か "streamEncoder.json/key"・"recordEncoder.json/key" の形。
// 一覧に無い項目は GuideOnly として扱う。
// RecFormat2 は old_value と new_value の群 (hybrid_mp4・hybrid_mov・それ以外) が同じときだけ Allowed
OutputSettingClass classify_output_setting(OutputMode mode, const std::string &key, const std::string &old_value,
					   const std::string &new_value);

// Video セクションの、obs_frontend_reset_video() で反映する項目
bool is_video_setting_key(const std::string &key);

// 録画形式の群。作り直しなしで切り替えられるのは同じ群の中だけ
int record_format_group(const std::string &format);

} // namespace aia
