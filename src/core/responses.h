#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aia {

// 使うモデルと推論の強さ。推論の強さは子課題 .3 の試験の結果で依頼者が決めるまでの仮の値
struct ModelConfig {
	std::string model = "gpt-6.1-terra";
	std::string reasoning_effort = "low";
	int max_output_tokens = 4000;
};

struct FunctionCall {
	std::string call_id;
	std::string name;
	// JSON の文字列のまま持つ。検査は道具ごとに行う
	std::string arguments;
};

struct Citation {
	std::string url;
	std::string title;
	int start_index = -1;
	int end_index = -1;
};

struct Usage {
	long long input_tokens = -1;
	long long cached_tokens = -1;
	long long output_tokens = -1;
	long long reasoning_tokens = -1;
	int web_search_requests = 0;
};

struct ParsedResponse {
	// HTTP 200 で、status が completed か incomplete のとき真
	bool ok = false;
	std::string status;
	// 次の要求の input に足す出力のアイテム。推論のアイテムも道具の結果とともに返す必要があるため、すべて持つ
	std::vector<nlohmann::json> output_items;
	std::vector<FunctionCall> function_calls;
	std::string text;
	std::vector<Citation> citations;
	int web_search_calls = 0;
	Usage usage;
	std::string incomplete_reason;
	std::string error_type;
	std::string error_code;
	std::string error_message;
};

// 会話の 1 回の要求の本文を作る。会話は手元に持つため store は false にする
nlohmann::json build_request(const ModelConfig &config, const std::string &instructions, const nlohmann::json &tools,
			     const std::vector<nlohmann::json> &input_items);

ParsedResponse parse_response(long http_status, const nlohmann::json &body);

nlohmann::json user_message_item(const std::string &text);
nlohmann::json function_call_output_item(const std::string &call_id, const std::string &output);

} // namespace aia
