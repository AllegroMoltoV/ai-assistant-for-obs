#include "tool-definitions.h"

#include "add-kinds.h"
#include "change-request.h"

namespace aia {

namespace {

using nlohmann::json;

json nullable_string(const char *description)
{
	return {{"type", json::array({"string", "null"})}, {"description", description}};
}

// strict のスキーマは、すべての項目を required に入れ、追加の項目を禁じる
json object_schema(json properties)
{
	json required = json::array();
	for (const auto &item : properties.items())
		required.push_back(item.key());
	return {{"type", "object"},
		{"properties", std::move(properties)},
		{"required", std::move(required)},
		{"additionalProperties", false}};
}

json function_tool(const char *name, const char *description, json parameters)
{
	return {{"type", "function"},
		{"name", name},
		{"description", description},
		{"parameters", std::move(parameters)},
		{"strict", true}};
}

json change_settings_schema()
{
	json targets = json::array();
	for (const auto &info : change_targets())
		targets.push_back(info.name);

	json change = object_schema({
		{"target",
		 {{"type", "string"},
		  {"enum", targets},
		  {"description",
		   "What to change. source_setting/filter_setting: a key in the source or filter settings. "
		   "source_visibility: show or hide a scene item. filter_enabled: enable or disable a filter. "
		   "audio_mute, audio_volume_db, audio_monitoring, audio_sync_offset_ms, audio_tracks: audio "
		   "properties of a source. output_setting: an output setting such as SimpleOutput/VBitrate or "
		   "streamEncoder.json/bitrate. video_setting: a key in the Video section such as Video/FPSCommon."}}},
		{"scene", nullable_string("Scene name. Required for source_visibility, otherwise null.")},
		{"source", nullable_string("Source name. Null for output_setting and video_setting.")},
		{"filter", nullable_string("Filter name. Required for filter_setting and filter_enabled.")},
		{"key", nullable_string("Setting key. Required for source_setting, filter_setting, output_setting and "
					"video_setting. For output_setting and video_setting use Section/Key or "
					"streamEncoder.json/key or recordEncoder.json/key.")},
		{"value_json",
		 {{"type", "string"},
		  {"description",
		   "The new value encoded as JSON, for example true, -6.5, \"monitor_only\", [1,2] or 4500."}}},
	});

	return object_schema({
		{"reason",
		 {{"type", "string"},
		  {"description", "Why this change should fix the problem, in the user's language."}}},
		{"changes", {{"type", "array"}, {"items", std::move(change)}, {"minItems", 1}}},
	});
}

json add_item_schema()
{
	json kinds = json::array();
	for (const auto &info : add_kinds())
		kinds.push_back(info.name);

	return object_schema({
		{"reason",
		 {{"type", "string"},
		  {"description", "Why this addition should fix the problem, in the user's language."}}},
		{"kind",
		 {{"type", "string"}, {"enum", kinds}, {"description", "What to add. Nothing else can be added."}}},
		{"source", nullable_string("For filters: the source to attach the filter to. Otherwise null.")},
		{"scene", nullable_string("For sources: the scene to place the new source in. Otherwise null.")},
		{"name", {{"type", "string"}, {"description", "Name of the new filter or source."}}},
		{"settings_json",
		 {{"type", "string"},
		  {"description",
		   "Initial settings encoded as a JSON object, for example {\"url\":\"https://...\"}. Use {} for defaults."}}},
	});
}

} // namespace

const std::vector<FunctionToolInfo> &function_tools()
{
	static const std::vector<FunctionToolInfo> tools = {
		{kToolGetObsStatus, ToolKind::Read},        {kToolListScenesAndSources, ToolKind::Read},
		{kToolGetSourceDetails, ToolKind::Read},    {kToolReadLog, ToolKind::Read},
		{kToolChangeSettings, ToolKind::Operation}, {kToolAddItem, ToolKind::Operation},
	};
	return tools;
}

const FunctionToolInfo *find_function_tool(const std::string &name)
{
	for (const auto &tool : function_tools()) {
		if (name == tool.name)
			return &tool;
	}
	return nullptr;
}

json build_tools_json()
{
	json tools = json::array();
	tools.push_back({{"type", "web_search"}});
	tools.push_back(function_tool(
		kToolGetObsStatus,
		"Get the current OBS Studio status: OBS version, OS, canvas and output resolution and FPS, output mode "
		"and output settings, whether streaming, recording, replay buffer and virtual camera are active, "
		"dropped frames, frames skipped due to encoding lag, frames missed due to rendering lag, and CPU usage.",
		object_schema(json::object())));
	tools.push_back(function_tool(
		kToolListScenesAndSources,
		"List scenes and their items with visibility, and audio sources with volume, mute, monitoring, sync "
		"offset and tracks.",
		object_schema(json::object())));
	tools.push_back(function_tool(
		kToolGetSourceDetails,
		"Get the settings and filters of one source, and the selectable values of its properties such as "
		"capture methods and device lists.",
		object_schema({{"source", {{"type", "string"}, {"description", "Source name."}}}})));
	tools.push_back(function_tool(kToolReadLog,
				      "Read the last 300 lines of the current OBS log. Secrets are masked.",
				      object_schema(json::object())));
	tools.push_back(function_tool(
		kToolChangeSettings,
		"Propose changes to existing OBS settings. The user sees the before and after values and must approve "
		"before anything changes. Output and video settings cannot be changed while streaming, recording, the "
		"replay buffer or the virtual camera is active. Some output settings such as the encoder type cannot be "
		"applied by this tool; for those, explain the steps in the OBS settings window instead.",
		change_settings_schema()));
	tools.push_back(function_tool(
		kToolAddItem,
		"Propose adding a noise suppression, gain, compressor or crop filter, an application audio capture "
		"source, or a browser source. The user must approve before anything is added. Nothing else can be added.",
		add_item_schema()));
	return tools;
}

} // namespace aia
