#pragma once

#include <string>
#include <vector>

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

// 書き換えてよい config の項目のキーをすべて返す。録画形式のように値によって GuideOnly になる項目も含む
std::vector<std::string> writable_output_config_keys();

// 録画形式の群。作り直しなしで切り替えられるのは同じ群の中だけ
int record_format_group(const std::string &format);

// 書き換えた設定が反映される時期。根拠は docs/reports/2026-11-11-output-settings-apply.md の考察の表
enum class ApplyTiming {
	// 適用した時点で反映される。出力と映像の設定以外
	Immediate,
	// 解像度と FPS。適用の中で obs_frontend_reset_video() を呼び、すぐ反映する
	VideoReset,
	// 配信・録画・リプレイバッファ・仮想カメラがすべて止まった状態からの、次の配信・録画の開始時
	NextStartAllStopped,
	// 次の配信・録画の開始時
	NextStart,
	// 次の配信の開始時
	NextStreamStart,
	// 次の録画の開始時
	NextRecordingStart,
};

// 書き換えてよい出力と映像の設定の項目が反映される時期。録画形式は old_value と new_value で決まる
ApplyTiming output_apply_timing(OutputMode mode, const std::string &key, const std::string &old_value,
				const std::string &new_value);

// モデルに返す、反映の時期の名前
const char *apply_timing_name(ApplyTiming timing);

// 承認の画面に出す、反映の時期の一文の訳のキー。Immediate は一文を出さないため nullptr
const char *apply_timing_notice_key(ApplyTiming timing);

} // namespace aia
