#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace aia {

// OBS の値と、道具の引数や結果で使う値の変換。libobs に依存しない部分だけを置く

// OBS 33 はモニタリングを有効と無効の 2 値で持ち、有効は出力にも流す (OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT 相当)
const char *monitoring_name(bool enabled);
// 道具の値をモニタリングの有効に直す。未知の値は偽を返す
bool monitoring_enabled_from_name(const std::string &name, bool &enabled);

// 音声トラックの番号 (1-6) の配列と、obs_source_get_audio_mixers のビット (トラック 1 が最下位) の対応
uint32_t tracks_to_mixers(const nlohmann::json &tracks);
nlohmann::json mixers_to_tracks(uint32_t mixers);

// 出力と映像の設定のキー。"Section/Key" はプロファイルの config、"streamEncoder.json/key" などはプロファイルのフォルダーの JSON
struct OutputKey {
	enum class Kind { Config, StreamEncoderJson, RecordEncoderJson };
	Kind kind = Kind::Config;
	// Config ならセクション、JSON ならファイル名
	std::string container;
	std::string name;
};
bool parse_output_key(const std::string &key, OutputKey &out);

// config は値を文字列で持つ。文字列はそのまま、数と真偽は OBS の config と同じ書き方にする。配列・オブジェクト・null は偽
bool to_config_text(const nlohmann::json &value, std::string &out);

// Video セクションの値から、obs_frontend_reset_video() の後に obs_get_video_info() が返すはずの値を求める。
// OBSBasic::ResetVideo と GetConfigFPS、obs_reset_video の丸めに合わせる
struct VideoConfig {
	uint64_t base_cx = 0;
	uint64_t base_cy = 0;
	uint64_t output_cx = 0;
	uint64_t output_cy = 0;
	uint64_t fps_type = 0;
	std::string fps_common;
	uint64_t fps_int = 0;
	uint64_t fps_num = 0;
	uint64_t fps_den = 0;
};
struct VideoExpectation {
	uint32_t base_width = 0;
	uint32_t base_height = 0;
	uint32_t output_width = 0;
	uint32_t output_height = 0;
	uint32_t fps_num = 0;
	uint32_t fps_den = 0;
};
VideoExpectation expected_video(const VideoConfig &config);

// ログのフォルダーのファイル名から現在のログを選ぶ。OBS の "YYYY-MM-DD HH-MM-SS.txt" の形だけを対象にし、最も新しいものを返す。
// 無ければ空
std::string pick_latest_log_file(const std::vector<std::string> &names);

// 末尾の count 行を返す。改行は LF と CRLF のどちらでもよく、最後の空行は数えない
std::string last_lines(const std::string &text, size_t count);

// 先頭の時刻 "HH:MM:SS.mmm: " と、引用符 '...' で囲んだ名前を除くと同じ文になる行を、最後に現れた位置の 1 行にまとめる。
// まとめた行の末尾には回数と最初の時刻を添える。同じエラーの繰り返しで、診断に役立つ行が末尾の範囲から押し出されるのを防ぐ。
// 空の行はまとめない。改行は LF と CRLF のどちらでもよく、結果は LF で返す
std::string collapse_repeated_lines(const std::string &text);

// OBS の色の整数は 0xAABBGGRR の順 (plugins/image-source/color-source.c と libobs の vec4_from_rgba)。
// 承認の画面とモデルには "#RRGGBB" で見せ、モデルからも "#RRGGBB" か "#RRGGBBAA" で受け取る
std::string obs_color_hex(long long value);
// 不透明度 (0-255)
int obs_color_alpha(long long value);
// "#RRGGBB" (不透明) か "#RRGGBBAA" を OBS の色の整数にする。形が違えば false
bool parse_color_hex(const std::string &text, long long &out);

} // namespace aia
