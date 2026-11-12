#include "add-kinds.h"

namespace aia {

const std::vector<AddKindInfo> &add_kinds()
{
	// ID の根拠は docs/reports/2026-11-11-openai-api-and-obs-behavior.md
	static const std::vector<AddKindInfo> kinds = {
		{AddKind::NoiseSuppression, "noise_suppression", "noise_suppress_filter_v2", true},
		{AddKind::Gain, "gain", "gain_filter", true},
		{AddKind::Compressor, "compressor", "compressor_filter", true},
		{AddKind::Crop, "crop", "crop_filter", true},
		{AddKind::ApplicationAudioCapture, "application_audio_capture", "wasapi_process_output_capture", false},
		{AddKind::BrowserSource, "browser_source", "browser_source", false},
		// 色ソースは版 3 が現行。libobs は版を付けた ID で登録する (plugins/image-source/color-source.c)
		{AddKind::ColorSource, "color_source", "color_source_v3", false},
	};
	return kinds;
}

const AddKindInfo *find_add_kind(const std::string &name)
{
	for (const auto &info : add_kinds()) {
		if (name == info.name)
			return &info;
	}
	return nullptr;
}

nlohmann::json added_mark(const std::string &name, const std::string &parent)
{
	return {{kAddedMarkKey, true}, {kAddedMarkNameKey, name}, {kAddedMarkParentKey, parent}};
}

bool has_added_mark(const nlohmann::json &private_settings, const std::string &name, const std::string &parent)
{
	if (!private_settings.is_object())
		return false;
	const auto flag = private_settings.find(kAddedMarkKey);
	const auto marked_name = private_settings.find(kAddedMarkNameKey);
	const auto marked_parent = private_settings.find(kAddedMarkParentKey);
	if (flag == private_settings.end() || !flag->is_boolean() || !flag->get<bool>())
		return false;
	if (marked_name == private_settings.end() || !marked_name->is_string() || *marked_name != name)
		return false;
	return marked_parent != private_settings.end() && marked_parent->is_string() && *marked_parent == parent;
}

} // namespace aia
