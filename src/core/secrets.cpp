#include "secrets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <regex>
#include <vector>

namespace aia {

namespace {

using nlohmann::json;

// 項目名の最後の語がこれらなら、値を秘密として伏せる
constexpr std::array<const char *, 16> kSecretWords = {
	"key",  "token",         "password", "passwd",  "pwd",        "pass",        "secret", "passphrase",
	"auth", "authorization", "cookie",   "cookies", "credential", "credentials", "apikey", "streamkey",
};

std::vector<std::string> split_words(const std::string &name)
{
	std::vector<std::string> words;
	std::string current;
	for (size_t i = 0; i < name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (!std::isalnum(c)) {
			if (!current.empty())
				words.push_back(current);
			current.clear();
			continue;
		}
		// camelCase の境目で分ける。例: streamKey → stream と key
		if (std::isupper(c) && !current.empty() && std::islower(static_cast<unsigned char>(current.back()))) {
			words.push_back(current);
			current.clear();
		}
		current.push_back(static_cast<char>(std::tolower(c)));
	}
	if (!current.empty())
		words.push_back(current);
	return words;
}

std::string replace_group(const std::string &text, const std::regex &pattern, size_t keep_group)
{
	std::string result;
	auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
	size_t last = 0;
	for (auto it = begin; it != std::sregex_iterator(); ++it) {
		const std::smatch &m = *it;
		const size_t start = static_cast<size_t>(m.position(0));
		result.append(text, last, start - last);
		if (keep_group > 0)
			result += m.str(keep_group);
		result += kMaskedValue;
		last = start + static_cast<size_t>(m.length(0));
	}
	result.append(text, last, std::string::npos);
	return result;
}

} // namespace

bool is_secret_field_name(const std::string &name)
{
	const auto words = split_words(name);
	if (words.empty())
		return false;
	const std::string &last = words.back();
	return std::any_of(kSecretWords.begin(), kSecretWords.end(), [&](const char *word) { return last == word; });
}

std::string mask_secrets_in_text(const std::string &text)
{
	// 配信の URL のアプリ名より後ろ。例: rtmp://live.twitch.tv/app/live_123 の live_123
	static const std::regex stream_path(R"(((?:rtmps?|srt|rist)://[^/\s"'<>]+/[^/\s"'<>?]+/)[^\s"'<>?]+)",
					    std::regex::icase);
	// URL の問い合わせ文字列の値
	static const std::regex query_value(R"(([?&][^=&#\s"'<>]+=)[^&#\s"'<>]+)");
	// 形で分かるキー
	static const std::regex bearer(R"((Bearer\s+)[A-Za-z0-9._~+/=\-]+)", std::regex::icase);
	static const std::regex openai_key(R"(sk-[A-Za-z0-9_\-]{16,})");
	static const std::regex twitch_key(R"(live_[0-9]+_[A-Za-z0-9]+)");
	static const std::regex youtube_key(R"(\b[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}\b)");

	std::string result = replace_group(text, stream_path, 1);
	result = replace_group(result, query_value, 1);
	result = replace_group(result, bearer, 1);
	result = replace_group(result, openai_key, 0);
	result = replace_group(result, twitch_key, 0);
	result = replace_group(result, youtube_key, 0);
	return result;
}

json mask_secrets_in_json(const json &value)
{
	if (value.is_string())
		return mask_secrets_in_text(value.get<std::string>());
	if (value.is_array()) {
		json result = json::array();
		for (const auto &item : value)
			result.push_back(mask_secrets_in_json(item));
		return result;
	}
	if (value.is_object()) {
		json result = json::object();
		for (const auto &item : value.items()) {
			const json &child = item.value();
			if (is_secret_field_name(item.key()) && child.is_string() && !child.get<std::string>().empty())
				result[item.key()] = kMaskedValue;
			else
				result[item.key()] = mask_secrets_in_json(child);
		}
		return result;
	}
	return value;
}

} // namespace aia
