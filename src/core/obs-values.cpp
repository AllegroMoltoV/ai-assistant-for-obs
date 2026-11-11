#include "obs-values.h"

#include "change-request.h"

#include <algorithm>
#include <cctype>

namespace aia {

const char *monitoring_name(bool enabled)
{
	return enabled ? kMonitoringAndOutput : kMonitoringOff;
}

bool monitoring_enabled_from_name(const std::string &name, bool &enabled)
{
	if (name == kMonitoringOff) {
		enabled = false;
		return true;
	}
	if (name == kMonitoringAndOutput) {
		enabled = true;
		return true;
	}
	return false;
}

uint32_t tracks_to_mixers(const nlohmann::json &tracks)
{
	uint32_t mixers = 0;
	if (!tracks.is_array())
		return mixers;
	for (const auto &track : tracks) {
		if (!track.is_number_integer())
			continue;
		const int number = track.get<int>();
		if (number >= 1 && number <= 6)
			mixers |= 1u << (number - 1);
	}
	return mixers;
}

nlohmann::json mixers_to_tracks(uint32_t mixers)
{
	nlohmann::json tracks = nlohmann::json::array();
	for (int number = 1; number <= 6; ++number) {
		if (mixers & (1u << (number - 1)))
			tracks.push_back(number);
	}
	return tracks;
}

bool parse_output_key(const std::string &key, OutputKey &out)
{
	const size_t slash = key.find('/');
	if (slash == std::string::npos || slash == 0 || slash + 1 >= key.size())
		return false;
	const std::string container = key.substr(0, slash);
	const std::string name = key.substr(slash + 1);
	// 入れ子のキーは扱わない。config のセクションと JSON の最上位のキーだけを対象にする
	if (name.find('/') != std::string::npos)
		return false;
	if (container == "streamEncoder.json")
		out.kind = OutputKey::Kind::StreamEncoderJson;
	else if (container == "recordEncoder.json")
		out.kind = OutputKey::Kind::RecordEncoderJson;
	else
		out.kind = OutputKey::Kind::Config;
	out.container = container;
	out.name = name;
	return true;
}

bool to_config_text(const nlohmann::json &value, std::string &out)
{
	if (value.is_string()) {
		out = value.get<std::string>();
		return true;
	}
	if (value.is_boolean()) {
		out = value.get<bool>() ? "true" : "false";
		return true;
	}
	if (value.is_number()) {
		out = value.dump();
		return true;
	}
	return false;
}

VideoExpectation expected_video(const VideoConfig &config)
{
	VideoExpectation v;
	v.base_width = static_cast<uint32_t>(config.base_cx);
	v.base_height = static_cast<uint32_t>(config.base_cy);
	v.output_width = static_cast<uint32_t>(config.output_cx);
	v.output_height = static_cast<uint32_t>(config.output_cy);
	if (v.base_width < 32 || v.base_height < 32) {
		v.base_width = 1920;
		v.base_height = 1080;
	}
	if (v.output_width < 32 || v.output_height < 32) {
		v.output_width = v.base_width;
		v.output_height = v.base_height;
	}
	v.output_width &= 0xFFFFFFFCu;
	v.output_height &= 0xFFFFFFFEu;

	if (config.fps_type == 1) {
		v.fps_num = static_cast<uint32_t>(config.fps_int);
		v.fps_den = 1;
	} else if (config.fps_type == 2) {
		v.fps_num = static_cast<uint32_t>(config.fps_num);
		v.fps_den = static_cast<uint32_t>(config.fps_den);
	} else {
		struct CommonFps {
			const char *name;
			uint32_t num;
			uint32_t den;
		};
		static const CommonFps common[] = {
			{"10", 10, 1},     {"20", 20, 1},          {"24 NTSC", 24000, 1001},
			{"25 PAL", 25, 1}, {"29.97", 30000, 1001}, {"48", 48, 1},
			{"50 PAL", 50, 1}, {"59.94", 60000, 1001}, {"60", 60, 1},
		};
		v.fps_num = 30;
		v.fps_den = 1;
		for (const auto &item : common) {
			if (config.fps_common == item.name) {
				v.fps_num = item.num;
				v.fps_den = item.den;
				break;
			}
		}
	}
	return v;
}

namespace {

// "YYYY-MM-DD HH-MM-SS.txt"
bool is_log_file_name(const std::string &name)
{
	static const char pattern[] = "dddd-dd-dd dd-dd-dd.txt";
	if (name.size() != sizeof(pattern) - 1)
		return false;
	for (size_t i = 0; i < name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (pattern[i] == 'd' ? !std::isdigit(c) : c != static_cast<unsigned char>(pattern[i]))
			return false;
	}
	return true;
}

} // namespace

std::string pick_latest_log_file(const std::vector<std::string> &names)
{
	// 桁数が固定のため、文字列の比較が日時の比較になる
	std::string latest;
	for (const auto &name : names) {
		if (is_log_file_name(name) && name > latest)
			latest = name;
	}
	return latest;
}

std::string last_lines(const std::string &text, size_t count)
{
	if (count == 0)
		return std::string();
	size_t end = text.size();
	// 末尾の改行は最後の行の終わりとして扱う
	if (end > 0 && text[end - 1] == '\n')
		--end;
	if (end > 0 && text[end - 1] == '\r')
		--end;
	size_t start = end;
	size_t lines = 0;
	while (start > 0) {
		if (text[start - 1] == '\n') {
			if (++lines == count)
				break;
		}
		--start;
	}
	std::string result = text.substr(start, end - start);
	result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
	return result;
}

} // namespace aia
