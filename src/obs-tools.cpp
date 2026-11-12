#include "obs-tools.h"

#include "core/display-names.h"
#include "core/obs-values.h"
#include "core/secrets.h"
#include "core/tool-definitions.h"
#include "core/tool-output.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <obs.hpp>
#include <plugin-support.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QApplication>
#include <QSysInfo>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace aia {

namespace {

using nlohmann::json;

constexpr size_t kLogLines = 300;

os_cpu_usage_info_t *g_cpu_info = nullptr;
uint64_t g_cpu_last_query_ns = 0;

struct PropertiesDeleter {
	void operator()(obs_properties_t *props) const { obs_properties_destroy(props); }
};
using PropertiesPtr = std::unique_ptr<obs_properties_t, PropertiesDeleter>;

std::string text_or_empty(const char *text)
{
	return text ? std::string(text) : std::string();
}

// OBS の文字列が UTF-8 として壊れていても例外にしない
std::string dump_json(const json &value)
{
	return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

json parse_or_object(const char *text)
{
	if (!text)
		return json::object();
	json value = json::parse(text, nullptr, false);
	return value.is_discarded() ? json::object() : value;
}

json data_to_json(obs_data_t *data, bool with_defaults)
{
	if (!data)
		return json::object();
	return parse_or_object(with_defaults ? obs_data_get_json_with_defaults(data) : obs_data_get_json(data));
}

// 1 つのキーだけを持つ obs_data を作る。JSON を経由して、数の整数と小数の区別を保つ
obs_data_t *single_value_data(const std::string &key, const json &value)
{
	return obs_data_create_from_json(dump_json(json{{key, value}}).c_str());
}

std::string finish_json(const json &value)
{
	return limit_head(dump_json(mask_secrets_in_json(value)));
}

std::string replace_all(std::string text, const std::string &from, const std::string &to)
{
	size_t pos = 0;
	while ((pos = text.find(from, pos)) != std::string::npos) {
		text.replace(pos, from.size(), to);
		pos += to.size();
	}
	return text;
}

// 利用者に見せる文言。ロケールの文字列の %1 などを置き換える
std::string localized(const char *key, const std::vector<std::string> &args)
{
	std::string text = obs_module_text(key);
	for (size_t i = 0; i < args.size(); ++i)
		text = replace_all(text, "%" + std::to_string(i + 1), args[i]);
	return text;
}

// OBS 本体の訳を引く。版の違いでキーが無いときは、OBS の訳を写したプラグインの "OBS.<キー>" を使う。
// obs_frontend_get_locale_string はキーが無いとき、渡したキーをそのまま返す
std::string obs_ui_text(const char *key)
{
	const char *text = obs_frontend_get_locale_string(key);
	if (text && *text && std::strcmp(text, key) != 0)
		return text;
	const std::string fallback = std::string("OBS.") + key;
	return obs_module_text(fallback.c_str());
}

std::string plugin_ui_text(const char *key)
{
	return obs_module_text(key);
}

const UiText &ui_text()
{
	static const UiText ui = {obs_ui_text, plugin_ui_text};
	return ui;
}

// 一覧のプロパティで、値に当たる選択肢の名前。見つからなければ空
std::string list_item_name(obs_property_t *p, const json &value)
{
	const obs_combo_format format = obs_property_list_format(p);
	const size_t count = obs_property_list_item_count(p);
	for (size_t i = 0; i < count; ++i) {
		bool match = false;
		switch (format) {
		case OBS_COMBO_FORMAT_INT:
			match = value.is_number() && obs_property_list_item_int(p, i) == value.get<long long>();
			break;
		case OBS_COMBO_FORMAT_FLOAT:
			match = value.is_number() &&
				std::fabs(obs_property_list_item_float(p, i) - value.get<double>()) < 1e-9;
			break;
		case OBS_COMBO_FORMAT_STRING:
			match = value.is_string() &&
				text_or_empty(obs_property_list_item_string(p, i)) == value.get<std::string>();
			break;
		case OBS_COMBO_FORMAT_BOOL:
			match = value.is_boolean() && obs_property_list_item_bool(p, i) == value.get<bool>();
			break;
		case OBS_COMBO_FORMAT_INVALID:
			break;
		}
		if (match)
			return text_or_empty(obs_property_list_item_name(p, i));
	}
	return std::string();
}

// プロパティの項目名。プロパティが見つからないときだけキーをそのまま使う
std::string property_label(obs_property_t *p, const std::string &key)
{
	const std::string description = p ? text_or_empty(obs_property_description(p)) : std::string();
	return description.empty() ? key : description;
}

// プロパティの値を、プロパティの画面と同じ表し方にする。一覧は選択肢の名前、数は OBS の単位を添える
std::string property_value_text(obs_property_t *p, const json &value)
{
	const UiText &ui = ui_text();
	if (!p || value.is_null())
		return plain_value_text(value, ui);
	switch (obs_property_get_type(p)) {
	case OBS_PROPERTY_LIST: {
		const std::string name = list_item_name(p, value);
		if (!name.empty())
			return name;
		break;
	}
	case OBS_PROPERTY_INT:
		if (value.is_number())
			return number_text(value, text_or_empty(obs_property_int_suffix(p)));
		break;
	case OBS_PROPERTY_FLOAT:
		if (value.is_number())
			return number_text(value, text_or_empty(obs_property_float_suffix(p)));
		break;
	default:
		break;
	}
	return plain_value_text(value, ui);
}

// 追加する項目の設定を「項目名: 値」の並びにする。設定が無ければ既定の設定と示す
std::string add_settings_text(const char *obs_id, const json &settings)
{
	if (!settings.is_object() || settings.empty())
		return obs_module_text("Proposal.DefaultSettings");
	PropertiesPtr props(obs_get_source_properties(obs_id));
	std::string out;
	for (auto it = settings.begin(); it != settings.end(); ++it) {
		obs_property_t *p = obs_properties_get(props.get(), it.key().c_str());
		if (!out.empty())
			out += obs_module_text("Value.ListSeparator");
		out += property_label(p, it.key()) + ": " + property_value_text(p, it.value());
	}
	return out;
}

json nullable_text(const std::string &text)
{
	return text.empty() ? json() : json(text);
}

// AI が追加したことを示す目印を、ソースかフィルタの private settings に書く
void write_added_mark(obs_source_t *source, const std::string &name, const std::string &parent)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	OBSDataAutoRelease mark = obs_data_create_from_json(dump_json(added_mark(name, parent)).c_str());
	obs_data_apply(priv, mark);
}

json added_mark_of(obs_source_t *source)
{
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	return data_to_json(priv, false);
}

double round_to(double value, double unit)
{
	return std::round(value / unit) * unit;
}

double volume_db(float mul)
{
	const double db = static_cast<double>(obs_mul_to_db(mul));
	if (!std::isfinite(db) || db < kMinVolumeDb)
		return kMinVolumeDb;
	return round_to(db, 0.1);
}

bool has_audio(obs_source_t *source)
{
	return (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

const char *target_name(ChangeTarget target)
{
	for (const auto &info : change_targets()) {
		if (info.target == target)
			return info.name;
	}
	return "";
}

const ChangeTargetInfo *target_info_by_name(const std::string &name)
{
	return find_change_target(name);
}

// ---------------------------------------------------------------------------
// プロファイルの出力の設定

OutputMode output_mode(config_t *config)
{
	const char *mode = config ? config_get_string(config, "Output", "Mode") : nullptr;
	return mode && std::strcmp(mode, "Advanced") == 0 ? OutputMode::Advanced : OutputMode::Simple;
}

std::string profile_file_path(const std::string &file)
{
	BPtr<char> dir = obs_frontend_get_current_profile_path();
	if (!dir)
		return std::string();
	return std::string(dir.Get()) + "/" + file;
}

// 詳細出力のエンコーダーの設定の JSON が対象とするエンコーダーの ID。録画が配信のエンコーダーを使う設定なら空
std::string encoder_id_for(config_t *config, OutputKey::Kind kind)
{
	const char *id = config_get_string(config, "AdvOut",
					   kind == OutputKey::Kind::StreamEncoderJson ? "Encoder" : "RecEncoder");
	const std::string value = text_or_empty(id);
	return value == "none" ? std::string() : value;
}

json read_profile_json(const std::string &file)
{
	const std::string path = profile_file_path(file);
	if (path.empty())
		return json::object();
	OBSDataAutoRelease data = obs_data_create_from_json_file(path.c_str());
	return data_to_json(data, false);
}

json config_values(config_t *config, const char *section, const std::vector<const char *> &names)
{
	json values = json::object();
	for (const char *name : names) {
		const char *value = config_get_string(config, section, name);
		if (value)
			values[std::string(section) + "/" + name] = value;
	}
	return values;
}

OutputActivity activity_now()
{
	OutputActivity activity;
	activity.streaming = obs_frontend_streaming_active();
	activity.recording = obs_frontend_recording_active();
	activity.replay_buffer = obs_frontend_replay_buffer_active();
	activity.virtual_camera = obs_frontend_virtualcam_active();
	activity.video_active = obs_video_active();
	return activity;
}

// 出力と映像の設定を書き込める状態か。書き込みと取り消しの直前に確かめる
bool output_settings_writable(std::string &error)
{
	if (activity_now().any()) {
		error = "Output and video settings cannot be changed while streaming, recording, the replay buffer or "
			"the virtual camera is active. Ask the user to stop them first.";
		return false;
	}
	if (settings_dialog_open()) {
		error = "The OBS settings window is open. Ask the user to close it first, because it may overwrite "
			"output settings when it is closed with OK.";
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 映像の設定の反映

VideoConfig read_video_config(config_t *config)
{
	VideoConfig v;
	v.base_cx = config_get_uint(config, "Video", "BaseCX");
	v.base_cy = config_get_uint(config, "Video", "BaseCY");
	v.output_cx = config_get_uint(config, "Video", "OutputCX");
	v.output_cy = config_get_uint(config, "Video", "OutputCY");
	v.fps_type = config_get_uint(config, "Video", "FPSType");
	v.fps_common = text_or_empty(config_get_string(config, "Video", "FPSCommon"));
	v.fps_int = config_get_uint(config, "Video", "FPSInt");
	v.fps_num = config_get_uint(config, "Video", "FPSNum");
	v.fps_den = config_get_uint(config, "Video", "FPSDen");
	return v;
}

// obs_frontend_reset_video() は戻り値が無いため、呼んだ後の映像の情報と config から求めた値を比べて成否を決める
bool reset_video_and_verify(std::string &error)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		error = "The OBS profile settings are not available.";
		return false;
	}
	obs_frontend_reset_video();
	const VideoExpectation expected = expected_video(read_video_config(config));
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		error = "OBS did not report the video settings after the reset.";
		return false;
	}
	if (ovi.base_width != expected.base_width || ovi.base_height != expected.base_height ||
	    ovi.output_width != expected.output_width || ovi.output_height != expected.output_height ||
	    ovi.fps_num != expected.fps_num || ovi.fps_den != expected.fps_den) {
		error = "OBS did not apply the new video settings. The values may be invalid or an output may have "
			"started.";
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 変更の 1 件の準備と書き込み

struct Located {
	OBSSourceAutoRelease source;
	OBSSourceAutoRelease filter;
};

bool locate_source(const Change &change, bool need_filter, Located &out, std::string &error)
{
	out.source = obs_get_source_by_name(change.source.c_str());
	if (!out.source) {
		error = "source not found: " + change.source;
		return false;
	}
	if (need_filter) {
		out.filter = obs_source_get_filter_by_name(out.source, change.filter.c_str());
		if (!out.filter) {
			error = "filter not found on " + change.source + ": " + change.filter;
			return false;
		}
	}
	return true;
}

obs_sceneitem_t *find_scene_item(obs_source_t *scene_source, const std::string &name)
{
	obs_scene_t *scene = obs_scene_from_source(scene_source);
	if (!scene)
		scene = obs_group_from_source(scene_source);
	return scene ? obs_scene_find_source_recursive(scene, name.c_str()) : nullptr;
}

Change change_from_entry(const json &entry)
{
	Change change;
	const ChangeTargetInfo *info = target_info_by_name(entry.value("target", ""));
	if (info)
		change.target = info->target;
	change.scene = entry.value("scene", "");
	change.source = entry.value("source", "");
	change.filter = entry.value("filter", "");
	change.key = entry.value("key", "");
	return change;
}

bool prepare_source_setting(const Change &change, json &entry, json &before, ProposalItem &item, std::string &error)
{
	const bool is_filter = change.target == ChangeTarget::FilterSetting;
	Located located;
	if (!locate_source(change, is_filter, located, error))
		return false;
	obs_source_t *target = is_filter ? located.filter.Get() : located.source.Get();
	OBSDataAutoRelease settings = obs_source_get_settings(target);
	const json current = data_to_json(settings, true);
	const bool in_settings = current.contains(change.key);
	PropertiesPtr props(obs_source_properties(target));
	obs_property_t *property = obs_properties_get(props.get(), change.key.c_str());
	if (!in_settings && !property) {
		error = "unknown setting key " + change.key + ". Call " + kToolGetSourceDetails +
			" to see the available keys";
		return false;
	}
	before = in_settings ? current[change.key] : json();
	entry["before_user_set"] = settings && obs_data_has_user_value(settings, change.key.c_str());
	const std::string name = property_label(property, change.key);
	item.label = is_filter ? localized("Proposal.FilterSetting", {change.source, change.filter, name})
			       : localized("Proposal.SourceSetting", {change.source, name});
	item.before = property_value_text(property, before);
	item.after = property_value_text(property, change.value);
	return true;
}

bool prepare_audio(const Change &change, json &entry, json &before, ProposalItem &item, std::string &error)
{
	Located located;
	if (!locate_source(change, false, located, error))
		return false;
	obs_source_t *source = located.source;
	if (!has_audio(source)) {
		error = "source has no audio: " + change.source;
		return false;
	}
	switch (change.target) {
	case ChangeTarget::AudioMute:
		before = obs_source_muted(source);
		break;
	case ChangeTarget::AudioVolumeDb: {
		const float mul = obs_source_get_volume(source);
		before = volume_db(mul);
		entry["before_mul"] = mul;
		break;
	}
	case ChangeTarget::AudioMonitoring: {
		bool enabled = false;
		if (!monitoring_enabled_from_name(change.value.get<std::string>(), enabled)) {
			error = "monitoring must be \"off\" or \"monitor_and_output\"";
			return false;
		}
		before = monitoring_name(obs_source_get_monitoring_enabled(source));
		break;
	}
	case ChangeTarget::AudioSyncOffsetMs: {
		const int64_t ns = obs_source_get_sync_offset(source);
		before = ns / 1000000;
		entry["before_ns"] = ns;
		break;
	}
	case ChangeTarget::AudioTracks: {
		const uint32_t mixers = obs_source_get_audio_mixers(source);
		before = mixers_to_tracks(mixers);
		entry["before_mixers"] = mixers;
		break;
	}
	default:
		error = "unsupported audio target";
		return false;
	}
	const UiText &ui = ui_text();
	item.label = localized("Proposal.AudioProperty", {change.source, ui.obs(audio_property_key(change.target))});
	item.before = audio_value_text(change.target, before, ui);
	item.after = audio_value_text(change.target, change.value, ui);
	return true;
}

bool prepare_output(const Change &change, const OutputActivity &activity, json &entry, json &before, ProposalItem &item,
		    std::string &error)
{
	if (settings_dialog_open()) {
		error = "The OBS settings window is open. Ask the user to close it first, because it may overwrite "
			"output settings when it is closed with OK.";
		return false;
	}
	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		error = "The OBS profile settings are not available.";
		return false;
	}
	OutputKey key;
	if (!parse_output_key(change.key, key)) {
		error = "key must be Section/Key, streamEncoder.json/key or recordEncoder.json/key";
		return false;
	}
	const OutputMode mode = output_mode(config);

	const UiText &ui = ui_text();
	std::string path;
	std::string old_text;
	std::string new_text;
	if (key.kind == OutputKey::Kind::Config) {
		if (!to_config_text(change.value, new_text)) {
			error = "value_json must be a string, number or boolean for this key";
			return false;
		}
		const char *current = config_get_string(config, key.container.c_str(), key.name.c_str());
		old_text = text_or_empty(current);
		before = current ? json(old_text) : json();
		path = output_setting_label(change.key, ui);
		item.before = output_setting_value(change.key, before, ui);
		item.after = output_setting_value(change.key, json(new_text), ui);
		entry["before_user_set"] = config_has_user_value(config, key.container.c_str(), key.name.c_str());
	}

	switch (decide_change(change, activity, mode, old_text, new_text)) {
	case ChangeDecision::Allowed:
		break;
	case ChangeDecision::BlockedWhileActive:
		error = "Output and video settings cannot be changed while streaming, recording, the replay buffer or "
			"the virtual camera is active. Ask the user to stop them first.";
		return false;
	case ChangeDecision::GuideOnly:
		error = "This setting cannot be applied by the plugin. Do not retry; guide the user through the steps "
			"in OBS Settings > Output (change it there and press OK while all outputs are stopped).";
		return false;
	}

	if (key.kind != OutputKey::Kind::Config) {
		const std::string encoder_id = encoder_id_for(config, key.kind);
		if (encoder_id.empty()) {
			error = "recordEncoder.json is not used because recording uses the stream encoder";
			return false;
		}
		PropertiesPtr props(obs_get_encoder_properties(encoder_id.c_str()));
		obs_property_t *property = obs_properties_get(props.get(), key.name.c_str());
		if (!property) {
			error = "the current encoder " + encoder_id + " has no setting " + key.name;
			return false;
		}
		const json current = read_profile_json(key.container);
		const bool present = current.is_object() && current.contains(key.name);
		if (present) {
			before = current[key.name];
		} else {
			OBSDataAutoRelease defaults = obs_encoder_defaults(encoder_id.c_str());
			const json values = data_to_json(defaults, true);
			before = values.contains(key.name) ? values[key.name] : json();
		}
		entry["before_present"] = present;
		path = encoder_setting_label(key.kind == OutputKey::Kind::RecordEncoderJson,
					     property_label(property, key.name), ui);
		item.before = property_value_text(property, before);
		item.after = property_value_text(property, change.value);
	}
	entry["file_or_section"] = key.container;
	entry["name"] = key.name;
	entry["json_file"] = key.kind != OutputKey::Kind::Config;
	entry["video"] = is_video_setting_key(change.key);
	item.timing = output_apply_timing(mode, change.key, old_text, new_text);
	item.label = localized(change.target == ChangeTarget::VideoSetting ? "Proposal.VideoSetting"
									   : "Proposal.OutputSetting",
			       {path.empty() ? change.key : path});
	return true;
}

bool prepare_change(const Change &change, const OutputActivity &activity, json &entry, ProposalItem &item,
		    std::string &error)
{
	entry = {{"target", target_name(change.target)},
		 {"scene", change.scene},
		 {"source", change.source},
		 {"filter", change.filter},
		 {"key", change.key},
		 {"after", change.value}};
	json before;
	const UiText &ui = ui_text();

	bool ok = false;
	switch (change.target) {
	case ChangeTarget::SourceSetting:
	case ChangeTarget::FilterSetting:
		ok = prepare_source_setting(change, entry, before, item, error);
		break;
	case ChangeTarget::SourceVisibility: {
		OBSSourceAutoRelease scene = obs_get_source_by_name(change.scene.c_str());
		obs_sceneitem_t *scene_item = scene ? find_scene_item(scene, change.source) : nullptr;
		if (!scene_item) {
			error = "scene item not found: " + change.source + " in " + change.scene;
			break;
		}
		before = obs_sceneitem_visible(scene_item);
		item.before = bool_value_text(before, ui.obs, "Show", "Hide", ui);
		item.after = bool_value_text(change.value, ui.obs, "Show", "Hide", ui);
		item.label = localized("Proposal.SourceVisibility", {change.scene, change.source});
		ok = true;
		break;
	}
	case ChangeTarget::FilterEnabled: {
		Located located;
		if (!locate_source(change, true, located, error))
			break;
		before = obs_source_enabled(located.filter);
		item.before = bool_value_text(before, ui.plugin, "Value.Enabled", "Value.Disabled", ui);
		item.after = bool_value_text(change.value, ui.plugin, "Value.Enabled", "Value.Disabled", ui);
		item.label = localized("Proposal.FilterEnabled", {change.source, change.filter});
		ok = true;
		break;
	}
	case ChangeTarget::AudioMute:
	case ChangeTarget::AudioVolumeDb:
	case ChangeTarget::AudioMonitoring:
	case ChangeTarget::AudioSyncOffsetMs:
	case ChangeTarget::AudioTracks:
		ok = prepare_audio(change, entry, before, item, error);
		break;
	case ChangeTarget::OutputSetting:
	case ChangeTarget::VideoSetting:
		ok = prepare_output(change, activity, entry, before, item, error);
		break;
	}
	entry["before"] = before;
	// change_settings の引数と同じ形で、変える前と後の値を記録する。元に戻す提案にそのまま使える
	item.record = {{"target", target_name(change.target)},       {"scene", nullable_text(change.scene)},
		       {"source", nullable_text(change.source)},     {"filter", nullable_text(change.filter)},
		       {"key", nullable_text(change.key)},           {"before_value_json", dump_json(before)},
		       {"after_value_json", dump_json(change.value)}};
	return ok;
}

bool is_output_entry(const json &entry)
{
	const std::string target = entry.value("target", "");
	return target == target_name(ChangeTarget::OutputSetting) || target == target_name(ChangeTarget::VideoSetting);
}

bool write_source_setting(const Change &change, const json &entry, bool to_after, std::string &error)
{
	const bool is_filter = change.target == ChangeTarget::FilterSetting;
	Located located;
	if (!locate_source(change, is_filter, located, error))
		return false;
	obs_source_t *target = is_filter ? located.filter.Get() : located.source.Get();
	if (to_after || entry.value("before_user_set", false)) {
		OBSDataAutoRelease data = single_value_data(change.key, to_after ? entry["after"] : entry["before"]);
		if (!data) {
			error = "the value could not be converted to OBS settings";
			return false;
		}
		obs_source_update(target, data);
		return true;
	}
	// 元は既定値だったため、利用者の値を消して既定値に戻す
	OBSDataAutoRelease settings = obs_source_get_settings(target);
	obs_data_erase(settings, change.key.c_str());
	obs_source_update(target, nullptr);
	return true;
}

bool write_audio(const Change &change, const json &entry, bool to_after, std::string &error)
{
	Located located;
	if (!locate_source(change, false, located, error))
		return false;
	obs_source_t *source = located.source;
	const json &value = to_after ? entry["after"] : entry["before"];
	switch (change.target) {
	case ChangeTarget::AudioMute:
		obs_source_set_muted(source, value.get<bool>());
		return true;
	case ChangeTarget::AudioVolumeDb:
		obs_source_set_volume(source,
				      to_after ? obs_db_to_mul(value.get<float>()) : entry["before_mul"].get<float>());
		return true;
	case ChangeTarget::AudioMonitoring: {
		bool enabled = false;
		if (!monitoring_enabled_from_name(value.get<std::string>(), enabled)) {
			error = "unsupported monitoring value";
			return false;
		}
		obs_source_set_monitoring_enabled(source, enabled);
		return true;
	}
	case ChangeTarget::AudioSyncOffsetMs:
		obs_source_set_sync_offset(source, to_after ? value.get<int64_t>() * 1000000
							    : entry["before_ns"].get<int64_t>());
		return true;
	case ChangeTarget::AudioTracks: {
		// トラック 7 以降のビットは道具の対象外のため、今の値を保つ
		const uint32_t current = obs_source_get_audio_mixers(source);
		const uint32_t mixers = to_after ? (current & ~0x3Fu) | tracks_to_mixers(value)
						 : entry["before_mixers"].get<uint32_t>();
		obs_source_set_audio_mixers(source, mixers);
		return true;
	}
	default:
		error = "unsupported audio target";
		return false;
	}
}

bool write_output(const json &entry, bool to_after, std::string &error)
{
	if (!output_settings_writable(error))
		return false;
	config_t *config = obs_frontend_get_profile_config();
	if (!config) {
		error = "The OBS profile settings are not available.";
		return false;
	}
	const std::string container = entry.value("file_or_section", "");
	const std::string name = entry.value("name", "");
	const json &value = to_after ? entry["after"] : entry["before"];

	if (!entry.value("json_file", false)) {
		if (to_after || entry.value("before_user_set", false)) {
			std::string text;
			if (!to_config_text(value, text)) {
				error = "value_json must be a string, number or boolean for this key";
				return false;
			}
			config_set_string(config, container.c_str(), name.c_str(), text.c_str());
		} else {
			config_remove_value(config, container.c_str(), name.c_str());
		}
		if (config_save_safe(config, "tmp", nullptr) != CONFIG_SUCCESS) {
			error = "the OBS profile settings could not be saved";
			return false;
		}
		return true;
	}

	const std::string path = profile_file_path(container);
	if (path.empty()) {
		error = "the OBS profile folder is not available";
		return false;
	}
	OBSDataAutoRelease data = obs_data_create_from_json_file(path.c_str());
	if (!data)
		data = obs_data_create();
	if (to_after || entry.value("before_present", false)) {
		OBSDataAutoRelease update = single_value_data(name, value);
		if (!update) {
			error = "the value could not be converted to OBS settings";
			return false;
		}
		obs_data_apply(data, update);
	} else {
		obs_data_erase(data, name.c_str());
	}
	// OBS の設定画面と同じく、一時ファイルを経由して書き、前の内容を .bak に残す
	if (!obs_data_save_json_safe(data, path.c_str(), "tmp", "bak")) {
		error = "the encoder settings file could not be saved: " + container;
		return false;
	}
	return true;
}

bool write_change(const json &entry, bool to_after, std::string &error)
{
	const Change change = change_from_entry(entry);
	switch (change.target) {
	case ChangeTarget::SourceSetting:
	case ChangeTarget::FilterSetting:
		return write_source_setting(change, entry, to_after, error);
	case ChangeTarget::SourceVisibility: {
		OBSSourceAutoRelease scene = obs_get_source_by_name(change.scene.c_str());
		obs_sceneitem_t *scene_item = scene ? find_scene_item(scene, change.source) : nullptr;
		if (!scene_item) {
			error = "scene item not found: " + change.source + " in " + change.scene;
			return false;
		}
		obs_sceneitem_set_visible(scene_item, (to_after ? entry["after"] : entry["before"]).get<bool>());
		return true;
	}
	case ChangeTarget::FilterEnabled: {
		Located located;
		if (!locate_source(change, true, located, error))
			return false;
		obs_source_set_enabled(located.filter, (to_after ? entry["after"] : entry["before"]).get<bool>());
		return true;
	}
	case ChangeTarget::AudioMute:
	case ChangeTarget::AudioVolumeDb:
	case ChangeTarget::AudioMonitoring:
	case ChangeTarget::AudioSyncOffsetMs:
	case ChangeTarget::AudioTracks:
		return write_audio(change, entry, to_after, error);
	case ChangeTarget::OutputSetting:
	case ChangeTarget::VideoSetting:
		return write_output(entry, to_after, error);
	}
	error = "unknown target";
	return false;
}

// 書き込んだ分を逆順に元へ戻す。戻せなかったものはログに残す
void restore_entries(const std::vector<const json *> &entries, bool to_after)
{
	bool video = false;
	for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
		std::string error;
		if (!write_change(**it, to_after, error))
			obs_log(LOG_WARNING, "could not restore a change: %s", error.c_str());
		video = video || (*it)->value("video", false);
	}
	if (video) {
		std::string error;
		if (!reset_video_and_verify(error))
			obs_log(LOG_WARNING, "could not restore the video settings: %s", error.c_str());
	}
}

// 変更の一覧を前から書き込む。失敗したら書き込んだ分を戻す
bool write_changes(const json &changes, std::string &error)
{
	if (!changes.is_array()) {
		error = "the proposal has no changes";
		return false;
	}
	std::vector<const json *> order;
	for (const auto &entry : changes)
		order.push_back(&entry);

	// 出力の設定が途中で書けずに一部だけ変わることを避けるため、先に確かめる
	const bool has_output =
		std::any_of(order.begin(), order.end(), [](const json *e) { return is_output_entry(*e); });
	if (has_output && !output_settings_writable(error))
		return false;

	std::vector<const json *> written;
	bool video = false;
	for (const json *entry : order) {
		if (!write_change(*entry, true, error)) {
			restore_entries(written, false);
			return false;
		}
		written.push_back(entry);
		video = video || entry->value("video", false);
	}
	if (video && !reset_video_and_verify(error)) {
		restore_entries(written, false);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 追加

bool add_kind_available(const AddKindInfo &kind, std::string &error)
{
	if (obs_source_get_display_name(kind.obs_id))
		return true;
	if (kind.kind == AddKind::ApplicationAudioCapture)
		error = "Application Audio Capture is not available. It requires Windows 10 version 2004 (build 19041) "
			"or later";
	else
		error = std::string("this kind is not available in this OBS: ") + kind.obs_id;
	return false;
}

bool add_item_now(const json &data, std::string &error)
{
	const std::string obs_id = data.value("obs_id", "");
	const std::string parent_name = data.value("parent", "");
	const std::string name = data.value("name", "");
	const AddKindInfo *kind = find_add_kind(data.value("kind_name", ""));
	if (!kind || obs_id != kind->obs_id) {
		error = "the proposal data is invalid";
		return false;
	}
	if (!add_kind_available(*kind, error))
		return false;

	OBSSourceAutoRelease parent = obs_get_source_by_name(parent_name.c_str());
	if (!parent) {
		error = (kind->is_filter ? "source not found: " : "scene not found: ") + parent_name;
		return false;
	}
	OBSDataAutoRelease settings =
		obs_data_create_from_json(dump_json(data.value("settings", json::object())).c_str());

	if (kind->is_filter) {
		OBSSourceAutoRelease existing = obs_source_get_filter_by_name(parent, name.c_str());
		if (existing) {
			error = "a filter with this name already exists on " + parent_name + ": " + name;
			return false;
		}
		OBSSourceAutoRelease filter = obs_source_create(obs_id.c_str(), name.c_str(), settings, nullptr);
		if (!filter) {
			error = "OBS could not create the filter";
			return false;
		}
		write_added_mark(filter, name, parent_name);
		obs_source_filter_add(parent, filter);
		return true;
	}

	obs_scene_t *scene = obs_scene_from_source(parent);
	if (!scene) {
		error = "scene not found: " + parent_name;
		return false;
	}
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing) {
		error = "a source with this name already exists: " + name;
		return false;
	}
	OBSSourceAutoRelease source = obs_source_create(obs_id.c_str(), name.c_str(), settings, nullptr);
	if (!source) {
		error = "OBS could not create the source";
		return false;
	}
	write_added_mark(source, name, std::string());
	if (!obs_scene_add(scene, source)) {
		error = "OBS could not add the source to the scene";
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// 削除。対象は AI が追加した目印のあるフィルタとソースに限る

// 削除の対象。フィルタなら付け先のソースとフィルタ、ソースならシーンの項目とそのソース
struct RemovalTarget {
	OBSSourceAutoRelease parent;
	OBSSourceAutoRelease filter;
	obs_sceneitem_t *scene_item = nullptr;
	obs_source_t *item = nullptr;
};

bool locate_removal(bool is_filter, const std::string &parent_name, const std::string &name, RemovalTarget &out,
		    std::string &error)
{
	out.parent = obs_get_source_by_name(parent_name.c_str());
	if (!out.parent) {
		error = (is_filter ? "source not found: " : "scene not found: ") + parent_name;
		return false;
	}
	if (is_filter) {
		out.filter = obs_source_get_filter_by_name(out.parent, name.c_str());
		if (!out.filter) {
			error = "filter not found on " + parent_name + ": " + name;
			return false;
		}
		out.item = out.filter;
	} else {
		obs_scene_t *scene = obs_scene_from_source(out.parent);
		out.scene_item = scene ? obs_scene_find_source(scene, name.c_str()) : nullptr;
		if (!out.scene_item) {
			error = "source not found in scene " + parent_name + ": " + name;
			return false;
		}
		out.item = obs_sceneitem_get_source(out.scene_item);
	}
	// 利用者が作ったものは削除しない。目印は追加したときに書いたもので、名前と付け先も一致する必要がある
	if (!has_added_mark(added_mark_of(out.item), name, is_filter ? parent_name : std::string())) {
		error = "This item was not added by the assistant, so it cannot be deleted. Suggest hiding, disabling "
			"or muting it instead.";
		return false;
	}
	return true;
}

// 承認の時点でも目印を確かめ直し、提案の後に別のものへ置き換わっていれば削除しない
bool remove_marked_item(const json &data, std::string &error)
{
	const bool is_filter = data.value("is_filter", false);
	RemovalTarget target;
	if (!locate_removal(is_filter, data.value("parent", ""), data.value("name", ""), target, error))
		return false;
	if (data.value("obs_id", "") != text_or_empty(obs_source_get_id(target.item))) {
		error = "the item changed after the proposal";
		return false;
	}
	if (is_filter)
		obs_source_filter_remove(target.parent, target.filter);
	else
		obs_sceneitem_remove(target.scene_item);
	return true;
}

// ---------------------------------------------------------------------------
// 取得

json list_options(obs_properties_t *props)
{
	json out = json::object();
	obs_property_t *p = obs_properties_first(props);
	while (p) {
		const obs_property_type type = obs_property_get_type(p);
		if (type == OBS_PROPERTY_GROUP) {
			const json nested = list_options(obs_property_group_content(p));
			out.update(nested);
		} else if (type == OBS_PROPERTY_LIST) {
			const obs_combo_format format = obs_property_list_format(p);
			json options = json::array();
			const size_t count = obs_property_list_item_count(p);
			for (size_t i = 0; i < count; ++i) {
				json option = {{"label", text_or_empty(obs_property_list_item_name(p, i))}};
				switch (format) {
				case OBS_COMBO_FORMAT_INT:
					option["value"] = obs_property_list_item_int(p, i);
					break;
				case OBS_COMBO_FORMAT_FLOAT:
					option["value"] = obs_property_list_item_float(p, i);
					break;
				case OBS_COMBO_FORMAT_STRING:
					option["value"] = text_or_empty(obs_property_list_item_string(p, i));
					break;
				case OBS_COMBO_FORMAT_BOOL:
					option["value"] = obs_property_list_item_bool(p, i);
					break;
				case OBS_COMBO_FORMAT_INVALID:
					break;
				}
				if (obs_property_list_item_disabled(p, i))
					option["disabled"] = true;
				options.push_back(std::move(option));
			}
			out[text_or_empty(obs_property_name(p))] = {{"description",
								     text_or_empty(obs_property_description(p))},
								    {"options", std::move(options)}};
		}
		obs_property_next(&p);
	}
	return out;
}

bool collect_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *items = static_cast<json *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	json entry = {{"name", text_or_empty(obs_source_get_name(source))},
		      {"type_id", text_or_empty(obs_source_get_id(source))},
		      {"visible", obs_sceneitem_visible(item)}};
	if (has_added_mark(added_mark_of(source), text_or_empty(obs_source_get_name(source)), std::string()))
		entry["added_by_assistant"] = true;
	if (obs_sceneitem_is_group(item)) {
		json children = json::array();
		obs_sceneitem_group_enum_items(item, collect_scene_item, &children);
		entry["items"] = std::move(children);
	}
	items->push_back(std::move(entry));
	return true;
}

bool collect_source(void *param, obs_source_t *source)
{
	// 列挙の間は OBS がソースの一覧をロックしているため、参照を取るだけにして後で読む
	static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
	return true;
}

void collect_filter(obs_source_t *parent, obs_source_t *filter, void *param)
{
	OBSDataAutoRelease settings = obs_source_get_settings(filter);
	const std::string name = text_or_empty(obs_source_get_name(filter));
	json entry = {{"name", name},
		      {"type_id", text_or_empty(obs_source_get_id(filter))},
		      {"enabled", obs_source_enabled(filter)},
		      {"settings", data_to_json(settings, true)}};
	if (has_added_mark(added_mark_of(filter), name, text_or_empty(obs_source_get_name(parent))))
		entry["added_by_assistant"] = true;
	static_cast<json *>(param)->push_back(std::move(entry));
}

double percent(uint64_t part, uint64_t total)
{
	return total ? round_to(100.0 * static_cast<double>(part) / static_cast<double>(total), 0.01) : 0.0;
}

} // namespace

void start_cpu_usage_monitor()
{
	if (!g_cpu_info) {
		g_cpu_info = os_cpu_usage_info_start();
		g_cpu_last_query_ns = os_gettime_ns();
	}
}

void stop_cpu_usage_monitor()
{
	os_cpu_usage_info_destroy(g_cpu_info);
	g_cpu_info = nullptr;
}

std::string read_obs_status()
{
	json out;
	out["obs_version"] = text_or_empty(obs_get_version_string());
	out["os"] = QSysInfo::prettyProductName().toStdString() + " (kernel " +
		    QSysInfo::kernelVersion().toStdString() + ", " + QSysInfo::currentCpuArchitecture().toStdString() +
		    ")";

	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi)) {
		out["video"] = {
			{"base_width_px", ovi.base_width},
			{"base_height_px", ovi.base_height},
			{"output_width_px", ovi.output_width},
			{"output_height_px", ovi.output_height},
			{"fps", ovi.fps_den ? round_to(static_cast<double>(ovi.fps_num) / ovi.fps_den, 0.01) : 0.0},
			{"fps_fraction", std::to_string(ovi.fps_num) + "/" + std::to_string(ovi.fps_den)},
		};
	}

	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		const OutputMode mode = output_mode(config);
		json output = {{"mode", mode == OutputMode::Advanced ? "Advanced" : "Simple"}};
		json settings = config_values(config, "Video",
					      {"BaseCX", "BaseCY", "OutputCX", "OutputCY", "FPSType", "FPSCommon",
					       "FPSInt", "FPSNum", "FPSDen"});
		if (mode == OutputMode::Simple) {
			settings.update(
				config_values(config, "SimpleOutput",
					      {"StreamEncoder", "VBitrate", "ABitrate", "Preset", "QSVPreset",
					       "NVENCPreset2", "AMDPreset", "AMDAV1Preset", "UseAdvanced",
					       "x264Settings", "RecFormat2", "RecQuality", "RecEncoder", "RecTracks"}));
		} else {
			settings.update(config_values(config, "AdvOut",
						      {"Encoder", "RecEncoder", "RecFormat2", "TrackIndex", "RecTracks",
						       "Rescale", "RescaleRes", "RecRescale", "RecRescaleRes",
						       "RecType", "Track1Bitrate", "Track2Bitrate", "Track3Bitrate",
						       "Track4Bitrate", "Track5Bitrate", "Track6Bitrate"}));
			output["streamEncoder.json"] = read_profile_json("streamEncoder.json");
			if (!encoder_id_for(config, OutputKey::Kind::RecordEncoderJson).empty())
				output["recordEncoder.json"] = read_profile_json("recordEncoder.json");
		}
		output["settings"] = std::move(settings);
		// 映像エンコーダの選択肢を設定画面と同じ名前で渡し、モデルが手順の案内で正しい名前を書けるようにする
		json choices = json::array();
		if (mode == OutputMode::Simple) {
			for (const auto &choice : simple_encoder_choices()) {
				if (obs_get_encoder_codec(choice.encoder_id))
					choices.push_back(
						{{"value", choice.value}, {"label", obs_ui_text(choice.locale_key)}});
			}
		} else {
			const char *id = nullptr;
			for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i) {
				if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO ||
				    (obs_get_encoder_caps(id) &
				     (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)))
					continue;
				choices.push_back(
					{{"value", id}, {"label", text_or_empty(obs_encoder_get_display_name(id))}});
			}
		}
		output["video_encoder_choices"] = std::move(choices);
		out["output"] = std::move(output);
	}

	// 配信サービスの設定には配信キーが入るため、finish_json で伏せてから返す
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (service) {
		OBSDataAutoRelease service_settings = obs_service_get_settings(service);
		out["streaming_service"] = {{"type_id", text_or_empty(obs_service_get_id(service))},
					    {"settings", data_to_json(service_settings, false)}};
	}

	const OutputActivity activity = current_output_activity();
	out["active"] = {{"streaming", activity.streaming},
			 {"recording", activity.recording},
			 {"replay_buffer", activity.replay_buffer},
			 {"virtual_camera", activity.virtual_camera},
			 {"any_output_video_active", activity.video_active}};

	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();
	if (stream_output && activity.streaming) {
		const int dropped = obs_output_get_frames_dropped(stream_output);
		const int total = obs_output_get_total_frames(stream_output);
		out["streaming_network"] = {
			{"dropped_frames", dropped},
			{"total_frames", total},
			{"dropped_percent", percent(static_cast<uint64_t>(std::max(dropped, 0)),
						    static_cast<uint64_t>(std::max(total, 0)))},
			{"congestion_0_to_1",
			 round_to(static_cast<double>(obs_output_get_congestion(stream_output)), 0.01)},
		};
	}

	// OBS の起動か映像の設定の変更からの累計
	const uint32_t rendered = obs_get_total_frames();
	const uint32_t lagged = obs_get_lagged_frames();
	out["rendering_lag"] = {{"missed_frames", lagged},
				{"total_frames", rendered},
				{"missed_percent", percent(lagged, rendered)},
				{"counted_since", "OBS start"}};
	video_t *video = obs_get_video();
	if (video) {
		const uint32_t encoded = video_output_get_total_frames(video);
		const uint32_t skipped = video_output_get_skipped_frames(video);
		out["encoding_lag"] = {{"skipped_frames", skipped},
				       {"total_frames", encoded},
				       {"skipped_percent", percent(skipped, encoded)},
				       {"counted_since", "last video reset"}};
	}

	out["active_fps"] = round_to(obs_get_active_fps(), 0.01);
	out["average_render_time_ms"] = round_to(static_cast<double>(obs_get_average_frame_time_ns()) / 1e6, 0.01);
	out["memory_usage_mb"] = round_to(static_cast<double>(os_get_proc_resident_size()) / (1024.0 * 1024.0), 0.1);
	if (g_cpu_info) {
		const uint64_t now = os_gettime_ns();
		out["cpu_usage_percent"] = round_to(os_cpu_usage_info_query(g_cpu_info), 0.1);
		out["cpu_usage_averaged_over_s"] = round_to(static_cast<double>(now - g_cpu_last_query_ns) / 1e9, 0.1);
		g_cpu_last_query_ns = now;
	}
	return finish_json(out);
}

std::string read_scenes_and_sources()
{
	json out;
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	out["current_scene"] = current ? text_or_empty(obs_source_get_name(current)) : std::string();

	json scenes = json::array();
	obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *scene_source = list.sources.array[i];
		json items = json::array();
		obs_scene_t *scene = obs_scene_from_source(scene_source);
		if (scene)
			obs_scene_enum_items(scene, collect_scene_item, &items);
		scenes.push_back(
			{{"name", text_or_empty(obs_source_get_name(scene_source))}, {"items", std::move(items)}});
	}
	obs_frontend_source_list_free(&list);
	out["scenes"] = std::move(scenes);

	std::vector<OBSSource> sources;
	obs_enum_sources(collect_source, &sources);
	json audio = json::array();
	for (const auto &source : sources) {
		if (!has_audio(source))
			continue;
		audio.push_back({
			{"name", text_or_empty(obs_source_get_name(source))},
			{"type_id", text_or_empty(obs_source_get_id(source))},
			{"volume_db", volume_db(obs_source_get_volume(source))},
			{"muted", obs_source_muted(source)},
			{"monitoring", monitoring_name(obs_source_get_monitoring_enabled(source))},
			{"sync_offset_ms", obs_source_get_sync_offset(source) / 1000000},
			{"tracks", mixers_to_tracks(obs_source_get_audio_mixers(source))},
		});
	}
	out["audio_sources"] = std::move(audio);
	return finish_json(out);
}

std::string read_source_details(const std::string &source_name)
{
	OBSSourceAutoRelease source = obs_get_source_by_name(source_name.c_str());
	if (!source)
		return finish_json({{"error", "source not found"}, {"source", source_name}});

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	json filters = json::array();
	obs_source_enum_filters(source, collect_filter, &filters);
	PropertiesPtr props(obs_source_properties(source));

	json out = {{"name", source_name},
		    {"type_id", text_or_empty(obs_source_get_id(source))},
		    {"settings", data_to_json(settings, true)},
		    {"filters", std::move(filters)},
		    {"property_options", props ? list_options(props.get()) : json::object()}};
	return finish_json(out);
}

std::string read_obs_log()
{
	BPtr<char> dir = os_get_config_path_ptr("obs-studio/logs");
	if (!dir)
		return "The OBS log folder could not be located.";

	std::vector<std::string> names;
	os_dir_t *handle = os_opendir(dir);
	if (!handle)
		return "The OBS log folder could not be opened.";
	while (os_dirent *entry = os_readdir(handle)) {
		if (!entry->directory)
			names.emplace_back(entry->d_name);
	}
	os_closedir(handle);

	const std::string latest = pick_latest_log_file(names);
	if (latest.empty())
		return "No OBS log file was found.";
	BPtr<char> content = os_quick_read_utf8_file((std::string(dir.Get()) + "/" + latest).c_str());
	if (!content)
		return "The OBS log file could not be read: " + latest;
	return limit_tail(mask_secrets_in_text(last_lines(collapse_repeated_lines(content.Get()), kLogLines)));
}

OutputActivity current_output_activity()
{
	return activity_now();
}

bool settings_dialog_open()
{
	const QWidget *modal = QApplication::activeModalWidget();
	return modal && modal->objectName() == QStringLiteral("OBSBasicSettings");
}

Parsed<Proposal> prepare_change_proposal(const ChangeRequest &request)
{
	Parsed<Proposal> result;
	Proposal &proposal = result.value;
	proposal.tool = kToolChangeSettings;
	proposal.reason = request.reason;

	const OutputActivity activity = current_output_activity();
	json entries = json::array();
	for (size_t i = 0; i < request.changes.size(); ++i) {
		json entry;
		ProposalItem item;
		std::string error;
		if (!prepare_change(request.changes[i], activity, entry, item, error)) {
			result.error = "changes[" + std::to_string(i) + "]: " + error;
			return result;
		}
		proposal.items.push_back(std::move(item));
		entries.push_back(std::move(entry));
		if ((activity.streaming || activity.recording) && changes_audio_immediately(request.changes[i].target))
			proposal.live_audio = true;
	}
	proposal.apply_data = {{"kind", "change"}, {"changes", std::move(entries)}};
	result.ok = true;
	return result;
}

Parsed<Proposal> prepare_add_proposal(const AddRequest &request)
{
	Parsed<Proposal> result;
	Proposal &proposal = result.value;
	proposal.tool = kToolAddItem;
	proposal.reason = request.reason;

	const AddKindInfo *kind = request.kind;
	if (!kind) {
		result.error = "kind is not one of the allowed kinds";
		return result;
	}
	if (!add_kind_available(*kind, result.error))
		return result;
	const OutputActivity activity = current_output_activity();
	proposal.live_audio = (activity.streaming || activity.recording) && changes_audio_immediately(kind->kind);

	const std::string parent_name = kind->is_filter ? request.source : request.scene;
	OBSSourceAutoRelease parent = obs_get_source_by_name(parent_name.c_str());
	const std::string type_name = text_or_empty(obs_source_get_display_name(kind->obs_id));
	ProposalItem item;
	if (kind->is_filter) {
		if (!parent) {
			result.error = "source not found: " + parent_name;
			return result;
		}
		OBSSourceAutoRelease existing = obs_source_get_filter_by_name(parent, request.name.c_str());
		if (existing) {
			result.error = "a filter with this name already exists on " + parent_name + ": " + request.name;
			return result;
		}
		// 音声のフィルターは音声を持つソースに、映像のフィルターは映像を持つソースにだけ付けられる
		const uint32_t needs = obs_get_source_output_flags(kind->obs_id) &
				       (OBS_SOURCE_AUDIO | OBS_SOURCE_VIDEO);
		if ((obs_source_get_output_flags(parent) & needs) != needs) {
			result.error =
				std::string("this filter cannot be attached to ") + parent_name +
				((needs & OBS_SOURCE_AUDIO) ? " because it has no audio" : " because it has no video");
			return result;
		}
		item.label = localized("Proposal.AddFilter", {parent_name, request.name, type_name});
	} else {
		if (!parent || !obs_scene_from_source(parent)) {
			result.error = "scene not found: " + parent_name;
			return result;
		}
		OBSSourceAutoRelease existing = obs_get_source_by_name(request.name.c_str());
		if (existing) {
			result.error = "a source with this name already exists: " + request.name;
			return result;
		}
		item.label = localized("Proposal.AddSource", {parent_name, request.name, type_name});
	}
	item.kind = ProposalItemKind::Add;
	item.after = add_settings_text(kind->obs_id, request.settings);
	// remove_item の引数と同じ形で記録する。追加を元に戻す提案にそのまま使える
	item.record = {{"kind", kind->is_filter ? "filter" : "source"},
		       {"source", kind->is_filter ? json(parent_name) : json()},
		       {"scene", kind->is_filter ? json() : json(parent_name)},
		       {"name", request.name}};
	proposal.items.push_back(std::move(item));
	proposal.apply_data = {{"kind", "add"},
			       {"kind_name", kind->name},
			       {"obs_id", kind->obs_id},
			       {"is_filter", kind->is_filter},
			       {"parent", parent_name},
			       {"name", request.name},
			       {"settings", request.settings}};
	result.ok = true;
	return result;
}

Parsed<Proposal> prepare_remove_proposal(const RemoveRequest &request)
{
	Parsed<Proposal> result;
	Proposal &proposal = result.value;
	proposal.tool = kToolRemoveItem;
	proposal.reason = request.reason;

	const std::string parent_name = request.is_filter ? request.source : request.scene;
	RemovalTarget target;
	if (!locate_removal(request.is_filter, parent_name, request.name, target, result.error))
		return result;
	const std::string obs_id = text_or_empty(obs_source_get_id(target.item));
	const std::string type_name = text_or_empty(obs_source_get_display_name(obs_id.c_str()));
	const OutputActivity activity = current_output_activity();
	proposal.live_audio = (activity.streaming || activity.recording) && has_audio(target.item);

	ProposalItem item;
	item.kind = ProposalItemKind::Remove;
	item.label = request.is_filter ? localized("Proposal.RemoveFilter", {parent_name, request.name, type_name})
				       : localized("Proposal.RemoveSource", {parent_name, request.name, type_name});
	item.record = {{"kind", request.is_filter ? "filter" : "source"},
		       {"source", nullable_text(request.source)},
		       {"scene", nullable_text(request.scene)},
		       {"name", request.name}};
	proposal.items.push_back(std::move(item));
	proposal.apply_data = {{"kind", "remove"},
			       {"obs_id", obs_id},
			       {"is_filter", request.is_filter},
			       {"parent", parent_name},
			       {"name", request.name}};
	result.ok = true;
	return result;
}

bool apply_proposal(const Proposal &proposal, std::string &error)
{
	const json &data = proposal.apply_data;
	const std::string kind = data.is_object() ? data.value("kind", "") : std::string();
	if (kind == "add")
		return add_item_now(data, error);
	if (kind == "remove")
		return remove_marked_item(data, error);
	if (kind == "change")
		return write_changes(data.contains("changes") ? data.at("changes") : json(), error);
	error = "the proposal data is invalid";
	return false;
}

} // namespace aia
