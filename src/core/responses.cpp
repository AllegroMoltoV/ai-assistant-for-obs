#include "responses.h"

namespace aia {

namespace {

using nlohmann::json;

std::string string_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

long long int_field(const json &object, const char *key)
{
	const auto it = object.find(key);
	return it != object.end() && it->is_number_integer() ? it->get<long long>() : -1;
}

long long detail_field(const json &object, const char *details_key, const char *key)
{
	const auto it = object.find(details_key);
	return it != object.end() && it->is_object() ? int_field(*it, key) : -1;
}

void read_message(const json &item, ParsedResponse &result)
{
	const auto content = item.find("content");
	if (content == item.end() || !content->is_array())
		return;
	for (const auto &part : *content) {
		if (string_field(part, "type") != "output_text")
			continue;
		// 出典の位置は本文の中の位置のため、複数の部分をつなぐときはずらす
		const int offset = static_cast<int>(result.text.size());
		result.text += string_field(part, "text");
		const auto annotations = part.find("annotations");
		if (annotations == part.end() || !annotations->is_array())
			continue;
		for (const auto &annotation : *annotations) {
			if (string_field(annotation, "type") != "url_citation")
				continue;
			Citation citation;
			citation.url = string_field(annotation, "url");
			citation.title = string_field(annotation, "title");
			const long long start = int_field(annotation, "start_index");
			const long long end = int_field(annotation, "end_index");
			citation.start_index = start >= 0 ? offset + static_cast<int>(start) : -1;
			citation.end_index = end >= 0 ? offset + static_cast<int>(end) : -1;
			if (!citation.url.empty())
				result.citations.push_back(std::move(citation));
		}
	}
}

} // namespace

json build_request(const ModelConfig &config, const std::string &instructions, const json &tools,
		   const std::vector<json> &input_items)
{
	return {
		{"model", config.model},
		{"reasoning", {{"effort", config.reasoning_effort}}},
		{"max_output_tokens", config.max_output_tokens},
		{"store", false},
		// store が false のため、推論の内容は暗号化して受け取り、次の要求で返す
		{"include", json::array({"reasoning.encrypted_content", "web_search_call.action.sources"})},
		{"instructions", instructions},
		{"tools", tools},
		{"input", input_items},
	};
}

ParsedResponse parse_response(long http_status, const json &body)
{
	ParsedResponse result;
	if (!body.is_object())
		return result;

	const auto error = body.find("error");
	if (error != body.end() && error->is_object()) {
		result.error_type = string_field(*error, "type");
		result.error_code = string_field(*error, "code");
		result.error_message = string_field(*error, "message");
	}

	result.status = string_field(body, "status");
	const auto incomplete = body.find("incomplete_details");
	if (incomplete != body.end() && incomplete->is_object())
		result.incomplete_reason = string_field(*incomplete, "reason");

	const auto usage = body.find("usage");
	if (usage != body.end() && usage->is_object()) {
		result.usage.input_tokens = int_field(*usage, "input_tokens");
		result.usage.cached_tokens = detail_field(*usage, "input_tokens_details", "cached_tokens");
		result.usage.output_tokens = int_field(*usage, "output_tokens");
		result.usage.reasoning_tokens = detail_field(*usage, "output_tokens_details", "reasoning_tokens");
	}
	const auto tool_usage = body.find("tool_usage");
	if (tool_usage != body.end() && tool_usage->is_object()) {
		const auto web_search = tool_usage->find("web_search");
		if (web_search != tool_usage->end() && web_search->is_object()) {
			const long long requests = int_field(*web_search, "num_requests");
			result.usage.web_search_requests = requests > 0 ? static_cast<int>(requests) : 0;
		}
	}

	const auto output = body.find("output");
	if (output != body.end() && output->is_array()) {
		for (const auto &item : *output) {
			result.output_items.push_back(item);
			const std::string type = string_field(item, "type");
			if (type == "function_call") {
				result.function_calls.push_back({string_field(item, "call_id"),
								 string_field(item, "name"),
								 string_field(item, "arguments")});
			} else if (type == "web_search_call") {
				++result.web_search_calls;
			} else if (type == "message") {
				read_message(item, result);
			}
		}
	}

	result.ok = http_status == 200 && (result.status == "completed" || result.status == "incomplete");
	return result;
}

json user_message_item(const std::string &text)
{
	return {{"role", "user"}, {"content", text}};
}

json function_call_output_item(const std::string &call_id, const std::string &output)
{
	return {{"type", "function_call_output"}, {"call_id", call_id}, {"output", output}};
}

} // namespace aia
