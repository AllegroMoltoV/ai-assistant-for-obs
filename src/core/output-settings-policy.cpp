#include "output-settings-policy.h"

#include <algorithm>
#include <array>

namespace aia {

namespace {

constexpr const char *kStreamEncoderJson = "streamEncoder.json/";
constexpr const char *kRecordEncoderJson = "recordEncoder.json/";

bool starts_with(const std::string &text, const char *prefix)
{
	return text.rfind(prefix, 0) == 0;
}

template<size_t N> bool contains(const std::array<const char *, N> &list, const std::string &key)
{
	return std::any_of(list.begin(), list.end(), [&](const char *item) { return key == item; });
}

// 単純出力で書き換えてよい項目。録画形式は群の検査を別に行う
constexpr std::array<const char *, 12> kSimpleAllowed = {
	"SimpleOutput/VBitrate",     "SimpleOutput/Preset",          "SimpleOutput/QSVPreset",
	"SimpleOutput/NVENCPreset2", "SimpleOutput/AMDPreset",       "SimpleOutput/AMDAV1Preset",
	"SimpleOutput/ABitrate",     "SimpleOutput/UseAdvanced",     "SimpleOutput/x264Settings",
	"SimpleOutput/RecTracks",    "SimpleOutput/VodTrackEnabled", "SimpleOutput/RecFormat2",
};

// 詳細出力で書き換えてよい config の項目。エンコーダーの設定の JSON は別に扱う
constexpr std::array<const char *, 15> kAdvancedAllowed = {
	"AdvOut/Track1Bitrate",
	"AdvOut/Track2Bitrate",
	"AdvOut/Track3Bitrate",
	"AdvOut/Track4Bitrate",
	"AdvOut/Track5Bitrate",
	"AdvOut/Track6Bitrate",
	"AdvOut/RescaleRes",
	"AdvOut/RescaleFilter",
	"AdvOut/RecRescaleRes",
	"AdvOut/RecRescaleFilter",
	"AdvOut/RecTracks",
	"AdvOut/FLVTrack",
	"AdvOut/StreamMultiTrackAudioMixes",
	"AdvOut/VodTrackEnabled",
	"AdvOut/RecFormat2",
};

constexpr std::array<const char *, 9> kVideoKeys = {
	"Video/BaseCX",    "Video/BaseCY", "Video/OutputCX", "Video/OutputCY", "Video/FPSType",
	"Video/FPSCommon", "Video/FPSInt", "Video/FPSNum",   "Video/FPSDen",
};

bool is_record_format_key(const std::string &key)
{
	return key == "SimpleOutput/RecFormat2" || key == "AdvOut/RecFormat2";
}

} // namespace

std::vector<std::string> writable_output_config_keys()
{
	std::vector<std::string> keys;
	keys.insert(keys.end(), kSimpleAllowed.begin(), kSimpleAllowed.end());
	keys.insert(keys.end(), kAdvancedAllowed.begin(), kAdvancedAllowed.end());
	keys.insert(keys.end(), kVideoKeys.begin(), kVideoKeys.end());
	return keys;
}

int record_format_group(const std::string &format)
{
	if (format == "hybrid_mp4")
		return 1;
	if (format == "hybrid_mov")
		return 2;
	return 0;
}

bool is_video_setting_key(const std::string &key)
{
	return contains(kVideoKeys, key);
}

OutputSettingClass classify_output_setting(OutputMode mode, const std::string &key, const std::string &old_value,
					   const std::string &new_value)
{
	if (is_video_setting_key(key))
		return OutputSettingClass::Allowed;

	if (is_record_format_key(key)) {
		const bool matches_mode = mode == OutputMode::Simple ? starts_with(key, "SimpleOutput/")
								     : starts_with(key, "AdvOut/");
		if (!matches_mode || record_format_group(old_value) != record_format_group(new_value))
			return OutputSettingClass::GuideOnly;
		return OutputSettingClass::Allowed;
	}

	if (mode == OutputMode::Simple)
		return contains(kSimpleAllowed, key) ? OutputSettingClass::Allowed : OutputSettingClass::GuideOnly;

	// エンコーダーの設定の JSON は、現在のエンコーダーが持つキーに限る。その検査は OBS とつなぐ層で行う
	if (starts_with(key, kStreamEncoderJson) || starts_with(key, kRecordEncoderJson))
		return key.find('/') + 1 < key.size() ? OutputSettingClass::Allowed : OutputSettingClass::GuideOnly;
	return contains(kAdvancedAllowed, key) ? OutputSettingClass::Allowed : OutputSettingClass::GuideOnly;
}

} // namespace aia
