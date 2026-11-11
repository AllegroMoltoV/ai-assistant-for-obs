#include "change-request.h"

namespace aia {

namespace {

using nlohmann::json;

std::string string_or_empty(const json &object, const char *key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string check_value(const ChangeTargetInfo &info, const json &value)
{
	switch (info.target) {
	case ChangeTarget::SourceVisibility:
	case ChangeTarget::FilterEnabled:
	case ChangeTarget::AudioMute:
		return value.is_boolean() ? std::string() : "value_json must be true or false";
	case ChangeTarget::AudioVolumeDb:
		if (!value.is_number())
			return "value_json must be a number in dB";
		if (value.get<double>() < kMinVolumeDb || value.get<double>() > kMaxVolumeDb)
			return "volume must be between -100 and 0 dB. Use a gain filter to amplify";
		return std::string();
	case ChangeTarget::AudioMonitoring: {
		const std::string v = value.is_string() ? value.get<std::string>() : std::string();
		if (v == kMonitoringOff || v == kMonitoringOnly || v == kMonitoringAndOutput)
			return std::string();
		return "value_json must be \"off\", \"monitor_only\" or \"monitor_and_output\"";
	}
	case ChangeTarget::AudioSyncOffsetMs:
		if (!value.is_number_integer())
			return "value_json must be an integer in milliseconds";
		if (value.get<long long>() < kMinSyncOffsetMs || value.get<long long>() > kMaxSyncOffsetMs)
			return "sync offset must be between -950 and 20000 ms";
		return std::string();
	case ChangeTarget::AudioTracks: {
		if (!value.is_array())
			return "value_json must be an array of enabled track numbers (1-6)";
		for (const auto &track : value) {
			if (!track.is_number_integer() || track.get<int>() < 1 || track.get<int>() > 6)
				return "track numbers must be integers from 1 to 6";
		}
		return std::string();
	}
	case ChangeTarget::SourceSetting:
	case ChangeTarget::FilterSetting:
	case ChangeTarget::OutputSetting:
	case ChangeTarget::VideoSetting:
		return value.is_null() ? "value_json must not be null" : std::string();
	}
	return "unknown target";
}

} // namespace

const std::vector<ChangeTargetInfo> &change_targets()
{
	static const std::vector<ChangeTargetInfo> targets = {
		{ChangeTarget::SourceSetting, "source_setting", false, true, false, true},
		{ChangeTarget::FilterSetting, "filter_setting", false, true, true, true},
		{ChangeTarget::SourceVisibility, "source_visibility", true, true, false, false},
		{ChangeTarget::FilterEnabled, "filter_enabled", false, true, true, false},
		{ChangeTarget::AudioMute, "audio_mute", false, true, false, false},
		{ChangeTarget::AudioVolumeDb, "audio_volume_db", false, true, false, false},
		{ChangeTarget::AudioMonitoring, "audio_monitoring", false, true, false, false},
		{ChangeTarget::AudioSyncOffsetMs, "audio_sync_offset_ms", false, true, false, false},
		{ChangeTarget::AudioTracks, "audio_tracks", false, true, false, false},
		{ChangeTarget::OutputSetting, "output_setting", false, false, false, true},
		{ChangeTarget::VideoSetting, "video_setting", false, false, false, true},
	};
	return targets;
}

const ChangeTargetInfo *find_change_target(const std::string &name)
{
	for (const auto &info : change_targets()) {
		if (name == info.name)
			return &info;
	}
	return nullptr;
}

Parsed<ChangeRequest> parse_change_settings(const std::string &arguments)
{
	Parsed<ChangeRequest> result;
	const json args = json::parse(arguments, nullptr, false);
	if (args.is_discarded() || !args.is_object()) {
		result.error = "arguments must be a JSON object";
		return result;
	}
	result.value.reason = string_or_empty(args, "reason");
	const auto changes = args.find("changes");
	if (changes == args.end() || !changes->is_array() || changes->empty()) {
		result.error = "changes must be a non-empty array";
		return result;
	}

	for (size_t i = 0; i < changes->size(); ++i) {
		const json &item = (*changes)[i];
		const std::string prefix = "changes[" + std::to_string(i) + "]: ";
		if (!item.is_object()) {
			result.error = prefix + "must be an object";
			return result;
		}
		const ChangeTargetInfo *info = find_change_target(string_or_empty(item, "target"));
		if (!info) {
			result.error = prefix + "unknown target";
			return result;
		}
		Change change;
		change.target = info->target;
		change.scene = string_or_empty(item, "scene");
		change.source = string_or_empty(item, "source");
		change.filter = string_or_empty(item, "filter");
		change.key = string_or_empty(item, "key");
		if ((info->needs_scene && change.scene.empty()) || (info->needs_source && change.source.empty()) ||
		    (info->needs_filter && change.filter.empty()) || (info->needs_key && change.key.empty())) {
			result.error = prefix + "missing scene, source, filter or key for target " + info->name;
			return result;
		}
		const std::string value_text = string_or_empty(item, "value_json");
		change.value = json::parse(value_text, nullptr, false);
		if (change.value.is_discarded()) {
			result.error = prefix + "value_json is not valid JSON";
			return result;
		}
		const std::string value_error = check_value(*info, change.value);
		if (!value_error.empty()) {
			result.error = prefix + value_error;
			return result;
		}
		result.value.changes.push_back(std::move(change));
	}
	result.ok = true;
	return result;
}

Parsed<AddRequest> parse_add_item(const std::string &arguments)
{
	Parsed<AddRequest> result;
	const json args = json::parse(arguments, nullptr, false);
	if (args.is_discarded() || !args.is_object()) {
		result.error = "arguments must be a JSON object";
		return result;
	}
	AddRequest &request = result.value;
	request.reason = string_or_empty(args, "reason");
	request.kind = find_add_kind(string_or_empty(args, "kind"));
	if (!request.kind) {
		result.error = "kind is not one of the allowed kinds";
		return result;
	}
	request.source = string_or_empty(args, "source");
	request.scene = string_or_empty(args, "scene");
	request.name = string_or_empty(args, "name");
	if (request.name.empty()) {
		result.error = "name must not be empty";
		return result;
	}
	if (request.kind->is_filter && request.source.empty()) {
		result.error = "source is required to add a filter";
		return result;
	}
	if (!request.kind->is_filter && request.scene.empty()) {
		result.error = "scene is required to add a source";
		return result;
	}
	const std::string settings_text = string_or_empty(args, "settings_json");
	if (!settings_text.empty()) {
		request.settings = json::parse(settings_text, nullptr, false);
		if (request.settings.is_discarded() || !request.settings.is_object()) {
			result.error = "settings_json must be a JSON object";
			return result;
		}
	}
	result.ok = true;
	return result;
}

} // namespace aia
