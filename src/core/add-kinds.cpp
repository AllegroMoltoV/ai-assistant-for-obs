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

} // namespace aia
