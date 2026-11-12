#include "stream.h"

#include <algorithm>

namespace aia {

namespace {

using nlohmann::json;

// 誤りの本文を読むための上限。SSE の応答の全体は持たない
constexpr size_t kRawLimit = 64 * 1024;

std::string string_field(const json &object, const char *key)
{
	const auto found = object.find(key);
	return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

} // namespace

void StreamReader::feed(const char *data, size_t size)
{
	if (raw_.size() < kRawLimit)
		raw_.append(data, std::min(size, kRawLimit - raw_.size()));
	pending_.append(data, size);
	size_t start = 0;
	for (;;) {
		const size_t end = pending_.find('\n', start);
		if (end == std::string::npos)
			break;
		std::string line = pending_.substr(start, end - start);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		handle_line(line);
		start = end + 1;
	}
	pending_.erase(0, start);
}

void StreamReader::handle_line(const std::string &line)
{
	// 空の行で 1 件が終わる。data 以外の行 (event: など) は、data の type で判断するため読まない
	if (line.empty()) {
		dispatch();
		return;
	}
	if (line.rfind("data:", 0) != 0)
		return;
	std::string value = line.substr(5);
	if (!value.empty() && value[0] == ' ')
		value.erase(0, 1);
	if (!data_.empty())
		data_ += '\n';
	data_ += value;
}

void StreamReader::dispatch()
{
	if (data_.empty())
		return;
	const json event = json::parse(data_, nullptr, false);
	data_.clear();
	if (!event.is_object())
		return;
	const std::string type = string_field(event, "type");
	if (type == "response.output_text.delta") {
		const std::string delta = string_field(event, "delta");
		if (!delta.empty() && on_delta_)
			on_delta_(delta);
	} else if (type == "response.completed" || type == "response.incomplete" || type == "response.failed") {
		const auto response = event.find("response");
		if (response != event.end() && response->is_object()) {
			final_ = *response;
			has_final_ = true;
		}
	} else if (type == "error") {
		// 応答の途中の誤り。stream でない誤りの本文と同じ形にする
		final_ = {{"status", "failed"},
			  {"error",
			   {{"code", string_field(event, "code")}, {"message", string_field(event, "message")}}}};
		has_final_ = true;
	}
}

json StreamReader::finish()
{
	// 最後の行が空行で終わっていなくても、届いた分は読む
	if (!pending_.empty()) {
		std::string line = pending_;
		pending_.clear();
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		handle_line(line);
	}
	dispatch();
	if (has_final_)
		return final_;
	return json::parse(raw_, nullptr, false);
}

} // namespace aia
