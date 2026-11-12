#include "display-names.h"

#include <cstdio>
#include <vector>

namespace aia {

namespace {

using nlohmann::json;

constexpr const char *kPathSeparator = " > ";

enum class ValueForm {
	Plain,
	Bool,
	// トラックのビット。トラック 1 が最下位
	TrackMask,
	// トラックの番号 (1-6)
	TrackNumber,
	// enum obs_scale_type の値
	ScaleFilter,
	RecordFormat,
	FpsType,
};

struct OutputSettingName {
	const char *key;
	// 設定画面の見出しから項目までの OBS の訳のキー
	std::vector<const char *> path;
	// 単位。区切りの空白を含める
	const char *unit;
	ValueForm form;
};

constexpr const char *kStreaming = "Basic.Settings.Output.Adv.Streaming";
constexpr const char *kRecording = "Basic.Settings.Output.Adv.Recording";
constexpr const char *kAudioTab = "Basic.Settings.Audio";
constexpr const char *kAudioBitrate = "Basic.Settings.Output.AudioBitrate";
constexpr const char *kRescale = "Basic.Settings.Output.Adv.Rescale";
constexpr const char *kAudioTrack = "Basic.Settings.Output.Adv.AudioTrack";
constexpr const char *kFormat = "Basic.Settings.Output.Format";
constexpr const char *kKbps = " kbps";
constexpr const char *kFps = " fps";

// 根拠は OBS 33.0.1 の frontend/forms/OBSBasicSettings.ui の見出しと項目名、
// frontend/settings/OBSBasicSettings.cpp の config の読み書き
const std::vector<OutputSettingName> &output_setting_names()
{
	static const std::vector<OutputSettingName> names = {
		{"SimpleOutput/VBitrate", {kStreaming, "Basic.Settings.Output.VideoBitrate"}, kKbps, ValueForm::Plain},
		{"SimpleOutput/Preset", {kStreaming, "Basic.Settings.Output.EncoderPreset"}, "", ValueForm::Plain},
		{"SimpleOutput/QSVPreset", {kStreaming, "Basic.Settings.Output.EncoderPreset"}, "", ValueForm::Plain},
		{"SimpleOutput/NVENCPreset2", {kStreaming, "Basic.Settings.Output.EncoderPreset"}, "", ValueForm::Plain},
		{"SimpleOutput/AMDPreset", {kStreaming, "Basic.Settings.Output.EncoderPreset"}, "", ValueForm::Plain},
		{"SimpleOutput/AMDAV1Preset", {kStreaming, "Basic.Settings.Output.EncoderPreset"}, "", ValueForm::Plain},
		{"SimpleOutput/ABitrate", {kStreaming, kAudioBitrate}, kKbps, ValueForm::Plain},
		{"SimpleOutput/UseAdvanced", {kStreaming, "Basic.Settings.Output.Advanced"}, "", ValueForm::Bool},
		{"SimpleOutput/x264Settings",
		 {kStreaming, "Basic.Settings.Output.CustomEncoderSettings"},
		 "",
		 ValueForm::Plain},
		{"SimpleOutput/RecTracks",
		 {kRecording, "Basic.Settings.Output.Simple.RecAudioTrack"},
		 "",
		 ValueForm::TrackMask},
		{"SimpleOutput/VodTrackEnabled",
		 {kStreaming, "Basic.Settings.Output.Simple.TwitchVodTrack"},
		 "",
		 ValueForm::Bool},
		{"SimpleOutput/RecFormat2", {kRecording, kFormat}, "", ValueForm::RecordFormat},
		{"AdvOut/Track1Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track1", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		{"AdvOut/Track2Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track2", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		{"AdvOut/Track3Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track3", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		{"AdvOut/Track4Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track4", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		{"AdvOut/Track5Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track5", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		{"AdvOut/Track6Bitrate",
		 {kAudioTab, "Basic.Settings.Output.Adv.Audio.Track6", kAudioBitrate},
		 kKbps,
		 ValueForm::Plain},
		// 出力をリスケールする の行は、左がフィルタ、右が解像度の 2 つの選択欄を持つ
		{"AdvOut/RescaleRes", {kStreaming, kRescale}, "", ValueForm::Plain},
		{"AdvOut/RescaleFilter", {kStreaming, kRescale}, "", ValueForm::ScaleFilter},
		{"AdvOut/RecRescaleRes", {kRecording, kRescale}, "", ValueForm::Plain},
		{"AdvOut/RecRescaleFilter", {kRecording, kRescale}, "", ValueForm::ScaleFilter},
		{"AdvOut/RecTracks", {kRecording, kAudioTrack}, "", ValueForm::TrackMask},
		// 録画フォーマットが FLV のときだけ、録画の音声トラックが 1 つを選ぶ形になる
		{"AdvOut/FLVTrack", {kRecording, kAudioTrack}, "", ValueForm::TrackNumber},
		{"AdvOut/StreamMultiTrackAudioMixes", {kStreaming, kAudioTrack}, "", ValueForm::TrackMask},
		{"AdvOut/VodTrackEnabled",
		 {kStreaming, "Basic.Settings.Output.Adv.TwitchVodTrack"},
		 "",
		 ValueForm::Bool},
		{"AdvOut/RecFormat2", {kRecording, kFormat}, "", ValueForm::RecordFormat},
		{"Video/BaseCX", {"Basic.Settings.Video.BaseResolution", "Width"}, "", ValueForm::Plain},
		{"Video/BaseCY", {"Basic.Settings.Video.BaseResolution", "Height"}, "", ValueForm::Plain},
		{"Video/OutputCX", {"Basic.Settings.Video.ScaledResolution", "Width"}, "", ValueForm::Plain},
		{"Video/OutputCY", {"Basic.Settings.Video.ScaledResolution", "Height"}, "", ValueForm::Plain},
		{"Video/FPSType", {"Basic.Settings.Video.FPS"}, "", ValueForm::FpsType},
		{"Video/FPSCommon", {"Basic.Settings.Video.FPSCommon"}, kFps, ValueForm::Plain},
		{"Video/FPSInt", {"Basic.Settings.Video.FPSInteger"}, kFps, ValueForm::Plain},
		{"Video/FPSNum",
		 {"Basic.Settings.Video.FPSFraction", "Basic.Settings.Video.Numerator"},
		 "",
		 ValueForm::Plain},
		{"Video/FPSDen",
		 {"Basic.Settings.Video.FPSFraction", "Basic.Settings.Video.Denominator"},
		 "",
		 ValueForm::Plain},
	};
	return names;
}

const OutputSettingName *find_output_setting_name(const std::string &key)
{
	for (const auto &name : output_setting_names()) {
		if (key == name.key)
			return &name;
	}
	return nullptr;
}

// 設定画面の録画フォーマットの選択肢。OBSBasicSettings::LoadFormats の FORMAT_STR
const char *record_format_key(const std::string &format)
{
	struct Entry {
		const char *value;
		const char *key;
	};
	static const Entry entries[] = {
		{"flv", "Basic.Settings.Output.Format.FLV"},
		{"mkv", "Basic.Settings.Output.Format.MKV"},
		{"mp4", "Basic.Settings.Output.Format.MP4"},
		{"mov", "Basic.Settings.Output.Format.MOV"},
		{"hybrid_mp4", "Basic.Settings.Output.Format.hMP4"},
		{"hybrid_mov", "Basic.Settings.Output.Format.hMOV"},
		{"fragmented_mp4", "Basic.Settings.Output.Format.fMP4"},
		{"fragmented_mov", "Basic.Settings.Output.Format.fMOV"},
		{"mpegts", "Basic.Settings.Output.Format.TS"},
		{"hls", "Basic.Settings.Output.Format.HLS"},
	};
	for (const auto &entry : entries) {
		if (format == entry.value)
			return entry.key;
	}
	return nullptr;
}

// 設定画面のリスケールのフィルタの選択肢。enum obs_scale_type の値で、OBS_SCALE_POINT (1) は選択肢に無い
const char *scale_filter_key(const std::string &value)
{
	if (value == "0")
		return "Basic.Settings.Output.Adv.Rescale.Disabled";
	if (value == "2")
		return "Basic.Settings.Video.DownscaleFilter.Bicubic";
	if (value == "3")
		return "Basic.Settings.Video.DownscaleFilter.Bilinear";
	if (value == "4")
		return "Basic.Settings.Video.DownscaleFilter.Lanczos";
	if (value == "5")
		return "Basic.Settings.Video.DownscaleFilter.Area";
	return nullptr;
}

const char *fps_type_key(const std::string &value)
{
	if (value == "0")
		return "Basic.Settings.Video.FPSCommon";
	if (value == "1")
		return "Basic.Settings.Video.FPSInteger";
	if (value == "2")
		return "Basic.Settings.Video.FPSFraction";
	return nullptr;
}

bool parse_uint(const std::string &text, unsigned long long &out)
{
	if (text.empty() || text.size() > 19)
		return false;
	unsigned long long value = 0;
	for (char c : text) {
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + static_cast<unsigned long long>(c - '0');
	}
	out = value;
	return true;
}

std::string track_name(int number, const UiText &ui)
{
	const std::string key = "Basic.Settings.Output.Adv.Audio.Track" + std::to_string(number);
	return ui.obs(key.c_str());
}

std::string join(const std::vector<std::string> &parts, const std::string &separator)
{
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i > 0)
			out += separator;
		out += parts[i];
	}
	return out;
}

// トラックの番号の配列を「トラック 1、トラック 2」の形にする。空なら「なし」
std::string tracks_text(const json &tracks, const UiText &ui)
{
	std::vector<std::string> names;
	for (const auto &track : tracks) {
		if (track.is_number_integer() && track.get<int>() >= 1 && track.get<int>() <= 6)
			names.push_back(track_name(track.get<int>(), ui));
		else
			names.push_back(plain_value_text(track, ui));
	}
	if (names.empty())
		return ui.plugin("Value.NoTracks");
	return join(names, ui.plugin("Value.ListSeparator"));
}

std::string config_value_text(const OutputSettingName &name, const std::string &value, const UiText &ui)
{
	const char *key = nullptr;
	unsigned long long number = 0;
	switch (name.form) {
	case ValueForm::Plain:
		return value + name.unit;
	case ValueForm::Bool:
		if (value == "true" || value == "1")
			return ui.obs("Yes");
		if (value == "false" || value == "0")
			return ui.obs("No");
		return value;
	case ValueForm::TrackMask: {
		if (!parse_uint(value, number))
			return value;
		json tracks = json::array();
		for (int track = 1; track <= 6; ++track) {
			if (number & (1ull << (track - 1)))
				tracks.push_back(track);
		}
		return tracks_text(tracks, ui);
	}
	case ValueForm::TrackNumber:
		if (parse_uint(value, number) && number >= 1 && number <= 6)
			return track_name(static_cast<int>(number), ui);
		return value;
	case ValueForm::ScaleFilter:
		key = scale_filter_key(value);
		break;
	case ValueForm::RecordFormat:
		key = record_format_key(value);
		break;
	case ValueForm::FpsType:
		key = fps_type_key(value);
		break;
	}
	return key ? ui.obs(key) : value;
}

} // namespace

std::string output_setting_label(const std::string &key, const UiText &ui)
{
	const OutputSettingName *name = find_output_setting_name(key);
	if (!name)
		return std::string();
	std::vector<std::string> parts;
	for (const char *part : name->path)
		parts.push_back(ui.obs(part));
	return join(parts, kPathSeparator);
}

std::string output_setting_value(const std::string &key, const json &value, const UiText &ui)
{
	const OutputSettingName *name = find_output_setting_name(key);
	if (!name || !value.is_string())
		return plain_value_text(value, ui);
	if (value.get<std::string>().empty())
		return "-";
	return config_value_text(*name, value.get<std::string>(), ui);
}

std::string encoder_setting_label(bool recording, const std::string &description, const UiText &ui)
{
	return join({ui.obs(recording ? kRecording : kStreaming), ui.obs("Basic.Settings.Output.Adv.Encoder"),
		     description},
		    kPathSeparator);
}

const char *audio_property_key(ChangeTarget target)
{
	switch (target) {
	case ChangeTarget::AudioMute:
		return "Mute";
	case ChangeTarget::AudioVolumeDb:
		return "Basic.AdvAudio.Volume";
	case ChangeTarget::AudioMonitoring:
		return "Basic.AdvAudio.Monitoring";
	case ChangeTarget::AudioSyncOffsetMs:
		return "Basic.AdvAudio.SyncOffset";
	case ChangeTarget::AudioTracks:
		return "Basic.AdvAudio.AudioTracks";
	default:
		return "";
	}
}

std::string audio_value_text(ChangeTarget target, const json &value, const UiText &ui)
{
	switch (target) {
	case ChangeTarget::AudioMute:
		return bool_value_text(value, ui.obs, "Mute", "Unmute", ui);
	case ChangeTarget::AudioVolumeDb:
		if (value.is_number()) {
			char text[32];
			std::snprintf(text, sizeof(text), "%.1f dB", value.get<double>());
			return text;
		}
		break;
	case ChangeTarget::AudioMonitoring:
		if (value == kMonitoringOff)
			return ui.obs("Basic.AdvAudio.Monitoring.None");
		if (value == kMonitoringAndOutput)
			return ui.obs("Basic.AdvAudio.Monitoring.Both");
		break;
	case ChangeTarget::AudioSyncOffsetMs:
		if (value.is_number())
			return number_text(value, " ms");
		break;
	case ChangeTarget::AudioTracks:
		if (value.is_array())
			return tracks_text(value, ui);
		break;
	default:
		break;
	}
	return plain_value_text(value, ui);
}

std::string bool_value_text(const json &value, const TextLookup &lookup, const char *true_key, const char *false_key,
			    const UiText &ui)
{
	if (!value.is_boolean())
		return plain_value_text(value, ui);
	return lookup(value.get<bool>() ? true_key : false_key);
}

std::string plain_value_text(const json &value, const UiText &ui)
{
	if (value.is_null())
		return "-";
	if (value.is_string())
		return value.get<std::string>();
	if (value.is_boolean())
		return ui.obs(value.get<bool>() ? "Yes" : "No");
	if (value.is_number())
		return number_text(value, "");
	std::vector<std::string> parts;
	if (value.is_array()) {
		for (const auto &element : value)
			parts.push_back(plain_value_text(element, ui));
	} else {
		for (auto it = value.begin(); it != value.end(); ++it)
			parts.push_back(it.key() + ": " + plain_value_text(it.value(), ui));
	}
	return join(parts, ui.plugin("Value.ListSeparator"));
}

std::string number_text(const json &value, const std::string &suffix)
{
	std::string text;
	if (value.is_number_unsigned())
		text = std::to_string(value.get<unsigned long long>());
	else if (value.is_number_integer())
		text = std::to_string(value.get<long long>());
	else if (value.is_number_float())
		text = value.dump();
	else if (value.is_string())
		text = value.get<std::string>();
	else
		return "-";
	return text + suffix;
}

const std::vector<SimpleEncoderChoice> &simple_encoder_choices()
{
	static const std::vector<SimpleEncoderChoice> choices = {
		{"x264", "Basic.Settings.Output.Simple.Encoder.Software", "obs_x264"},
		{"qsv", "Basic.Settings.Output.Simple.Encoder.Hardware.QSV.H264", "obs_qsv11"},
		{"qsv_av1", "Basic.Settings.Output.Simple.Encoder.Hardware.QSV.AV1", "obs_qsv11_av1"},
		{"nvenc", "Basic.Settings.Output.Simple.Encoder.Hardware.NVENC.H264", "ffmpeg_nvenc"},
		{"nvenc_av1", "Basic.Settings.Output.Simple.Encoder.Hardware.NVENC.AV1", "obs_nvenc_av1_tex"},
		{"amd_hevc", "Basic.Settings.Output.Simple.Encoder.Hardware.AMD.HEVC", "h265_texture_amf"},
		{"nvenc_hevc", "Basic.Settings.Output.Simple.Encoder.Hardware.NVENC.HEVC", "ffmpeg_hevc_nvenc"},
		{"amd", "Basic.Settings.Output.Simple.Encoder.Hardware.AMD.H264", "h264_texture_amf"},
		{"amd_av1", "Basic.Settings.Output.Simple.Encoder.Hardware.AMD.AV1", "av1_texture_amf"},
	};
	return choices;
}

} // namespace aia
