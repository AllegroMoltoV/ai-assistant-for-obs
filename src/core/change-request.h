#pragma once

#include "add-kinds.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aia {

// change_settings の 1 件の変更の対象
enum class ChangeTarget {
	SourceSetting,
	FilterSetting,
	SourceVisibility,
	FilterEnabled,
	AudioMute,
	AudioVolumeDb,
	AudioMonitoring,
	AudioSyncOffsetMs,
	AudioTracks,
	OutputSetting,
	VideoSetting,
};

struct ChangeTargetInfo {
	ChangeTarget target;
	// 道具の引数で使う名前
	const char *name;
	bool needs_scene;
	bool needs_source;
	bool needs_filter;
	bool needs_key;
};

const std::vector<ChangeTargetInfo> &change_targets();
const ChangeTargetInfo *find_change_target(const std::string &name);

// モニタリングの値。OBS 33 のモニタリングは有効と無効の 2 値で、有効は「モニターと出力」に当たる
// (libobs/obs.h の obs_source_set_monitoring_enabled。種類を選ぶ API は非推奨)
inline constexpr const char *kMonitoringOff = "off";
inline constexpr const char *kMonitoringAndOutput = "monitor_and_output";

// OBS の音量の操作の範囲。ミキサーの上限の 0 dB を超える増幅はゲインのフィルターで行う
inline constexpr double kMinVolumeDb = -100.0;
inline constexpr double kMaxVolumeDb = 0.0;
// OBS の詳細オーディオプロパティの同期オフセットの範囲 (ミリ秒)
inline constexpr long long kMinSyncOffsetMs = -950;
inline constexpr long long kMaxSyncOffsetMs = 20000;

struct Change {
	ChangeTarget target = ChangeTarget::SourceSetting;
	std::string scene;
	std::string source;
	std::string filter;
	std::string key;
	nlohmann::json value;
};

struct ChangeRequest {
	std::string reason;
	std::vector<Change> changes;
};

struct AddRequest {
	std::string reason;
	const AddKindInfo *kind = nullptr;
	// フィルターを付けるソース
	std::string source;
	// ソースを置くシーン
	std::string scene;
	std::string name;
	nlohmann::json settings = nlohmann::json::object();
};

// remove_item の要求。削除できるのは AI が追加したフィルタとソースだけで、その判定は OBS とつなぐ層で行う
struct RemoveRequest {
	std::string reason;
	bool is_filter = false;
	// フィルタの付け先のソース
	std::string source;
	// ソースを取り除くシーン
	std::string scene;
	std::string name;
};

template<typename T> struct Parsed {
	bool ok = false;
	T value;
	// モデルに返す、引数の誤りの説明
	std::string error;
};

// 関数の呼び出しの arguments (JSON の文字列) を検査して取り出す。
// OBS の状態に依存する検査 (ソースの有無、出力の稼働、出力の設定の可否) は OBS とつなぐ層で行う
Parsed<ChangeRequest> parse_change_settings(const std::string &arguments);
Parsed<AddRequest> parse_add_item(const std::string &arguments);
Parsed<RemoveRequest> parse_remove_item(const std::string &arguments);

} // namespace aia
