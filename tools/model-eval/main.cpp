// モデルを比べる開発用の道具。OBS を使わず、場面のファイルに用意した模した道具の結果と利用者の返答で、
// Responses API と多段の会話をする。プラグインと同じ道具の定義・指示文・要求の組み立て・応答の解釈を使う。
// API キーは .env の OPENAI_API_KEY= の行から読み、画面にもファイルにも出さない。
//
// 使い方:
//   aia-model-eval run <scenarios のフォルダー> <出力のフォルダー> <model:effort[:noweb]> [--only 接頭辞]
//                      [--budget 米ドル] [--env .env のパス]
//   aia-model-eval report <scenarios のフォルダー> <出力のフォルダー>
//   aia-model-eval language [.env のパス]  回答と要約の言語を、OBS の表示言語と利用者の言語の 4 通りで確かめる
// run は 1 つの構成で場面を順に回し、<出力>/runs/<構成>__<場面>.json に会話と usage を書く。
// report は runs の JSON を読み、自動の判定と集計を <出力>/outputs.md と stats.md に書く。

// curl.h が読む windows.h の min と max のマクロが std::min と std::max を壊すため止める
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "core/change-request.h"
#include "core/conversation.h"
#include "core/instructions.h"
#include "core/output-settings-policy.h"
#include "core/responses.h"
#include "core/tool-definitions.h"
#include "core/tool-output.h"
#include "openai-client.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
namespace fs = std::filesystem;
using namespace aia;

namespace {

// 1 つの場面で送る要求の上限。道具の呼び出しの往復を含む
constexpr int kMaxRequestsPerScenario = 12;
// 429 と 5xx のときに送り直す回数
constexpr int kMaxRetries = 2;
// Web 検索 1 回の料金 (米ドル)
constexpr double kWebSearchFeeUsd = 0.01;
constexpr const char *kDefaultEnvPath = "D:/work/ai-assistant-for-obs/.tmp/.env";

struct Price {
	// 100 万トークンあたりの米ドル。推論のトークンは出力に含まれる
	double input;
	double cached;
	double output;
};

Price price_for(const std::string &model)
{
	if (model == "gpt-6.1-terra")
		return {1.0, 0.05, 5.0};
	if (model == "gpt-6-luna")
		return {0.10, 0.01, 0.50};
	return {-1.0, -1.0, -1.0};
}

struct EvalConfig {
	std::string model;
	std::string effort;
	bool web_search = true;

	std::string label() const { return model + "-" + effort + (web_search ? "" : "-noweb"); }
};

bool parse_config(const std::string &text, EvalConfig &config)
{
	std::vector<std::string> parts;
	std::stringstream stream(text);
	std::string part;
	while (std::getline(stream, part, ':'))
		parts.push_back(part);
	if (parts.size() < 2 || parts.size() > 3)
		return false;
	config.model = parts[0];
	config.effort = parts[1];
	if (parts.size() == 3) {
		if (parts[2] != "noweb")
			return false;
		config.web_search = false;
	}
	return price_for(config.model).input >= 0.0;
}

double request_cost(const Price &price, const Usage &usage, int web_searches)
{
	const double input = static_cast<double>(std::max(0LL, usage.input_tokens));
	const double cached = static_cast<double>(std::max(0LL, usage.cached_tokens));
	const double output = static_cast<double>(std::max(0LL, usage.output_tokens));
	return ((input - cached) * price.input + cached * price.cached + output * price.output) / 1e6 +
	       web_searches * kWebSearchFeeUsd;
}

std::string read_file(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

void write_file(const fs::path &path, const std::string &text)
{
	std::ofstream out(path, std::ios::binary);
	out << text;
}

// .env の OPENAI_API_KEY= の行の値を返す。値は呼び出し元でも表示しない
std::string read_api_key(const fs::path &env_path)
{
	std::ifstream in(env_path, std::ios::binary);
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		const auto start = line.find_first_not_of(" \t");
		if (start == std::string::npos)
			continue;
		const std::string prefix = "OPENAI_API_KEY";
		if (line.compare(start, prefix.size(), prefix) != 0)
			continue;
		auto eq = line.find('=', start + prefix.size());
		if (eq == std::string::npos)
			continue;
		std::string value = line.substr(eq + 1);
		const auto first = value.find_first_not_of(" \t\"'");
		const auto last = value.find_last_not_of(" \t\"'");
		if (first == std::string::npos)
			return std::string();
		return value.substr(first, last - first + 1);
	}
	return std::string();
}

struct Scenario {
	std::string id;
	json data;
};

std::vector<Scenario> load_scenarios(const fs::path &dir)
{
	std::vector<fs::path> files;
	for (const auto &entry : fs::directory_iterator(dir)) {
		if (entry.path().extension() == ".json")
			files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end());
	std::vector<Scenario> scenarios;
	for (const auto &file : files) {
		const json data = json::parse(read_file(file), nullptr, false);
		if (data.is_discarded() || !data.is_object()) {
			std::fprintf(stderr, "invalid scenario: %s\n", file.string().c_str());
			continue;
		}
		scenarios.push_back({file.stem().string(), data});
	}
	return scenarios;
}

std::string value_as_text(const json &value)
{
	return value.is_string() ? value.get<std::string>() : value.dump();
}

// 推論の暗号化された中身を除いた写し。ログに載せるため
json without_secrets(json item)
{
	if (item.is_object()) {
		item.erase("encrypted_content");
		for (auto &entry : item.items())
			entry.value() = without_secrets(entry.value());
	} else if (item.is_array()) {
		for (auto &entry : item)
			entry = without_secrets(entry);
	}
	return item;
}

// 1 つの場面の模した OBS。道具の呼び出しに、場面のファイルの値で答える
class MockObs {
public:
	explicit MockObs(const json &scenario) : scenario_(scenario)
	{
		results_ = scenario.value("tool_results", json::object());
		outputs_active_ = scenario.value("outputs_active", false);
		mode_ = scenario.value("output_mode", std::string("Simple")) == "Advanced" ? OutputMode::Advanced
											   : OutputMode::Simple;
		for (const auto &item : scenario.value("current_output_settings", json::object()).items())
			output_settings_[item.key()] = value_as_text(item.value());
		const json lists = results_.value("list_scenes_and_sources", json::object());
		for (const auto &scene : lists.value("scenes", json::array())) {
			scenes_.insert(scene.value("name", std::string()));
			for (const auto &item : scene.value("items", json::array()))
				sources_.insert(item.value("source", std::string()));
		}
		for (const auto &audio : lists.value("audio_sources", json::array()))
			sources_.insert(audio.value("name", std::string()));
	}

	// 利用者が OBS の画面で設定を変えた場面を模す。場面のファイルの "tool_results_before_reply" の
	// "<返答の番号>" にある道具の結果で、その返答を送る前に置き換える
	void change_before_reply(size_t reply_index)
	{
		const json changes = scenario_.value("tool_results_before_reply", json::object());
		const auto it = changes.find(std::to_string(reply_index));
		if (it == changes.end() || !it->is_object())
			return;
		for (const auto &item : it->items())
			results_[item.key()] = item.value();
	}

	void stop_outputs()
	{
		outputs_active_ = false;
		if (results_.contains("get_obs_status") && results_["get_obs_status"].contains("active")) {
			for (auto &entry : results_["get_obs_status"]["active"].items())
				entry.value() = false;
			results_["get_obs_status"]["stats"].erase("stream_duration");
		}
	}

	// 関数の結果の文字列と、操作の提案の記録 (操作でなければ null) を返す
	std::string call(const FunctionCall &fc, json &proposal)
	{
		proposal = nullptr;
		if (fc.name == kToolGetObsStatus || fc.name == kToolListScenesAndSources) {
			const auto it = results_.find(fc.name);
			return limit_head(it != results_.end() ? it->dump()
							       : json({{"error", "not available"}}).dump());
		}
		if (fc.name == kToolGetSourceDetails) {
			const json args = json::parse(fc.arguments, nullptr, false);
			const std::string source = args.is_object() ? args.value("source", std::string())
								    : std::string();
			const json details = results_.value("get_source_details", json::object());
			const auto it = details.find(source);
			if (it != details.end())
				return limit_head(it->dump());
			return json({{"error", "source not found: " + source}}).dump();
		}
		if (fc.name == kToolReadLog) {
			std::string text;
			for (const auto &line : results_.value("read_log", json::array()))
				text += line.get<std::string>() + "\n";
			return limit_tail(text);
		}
		if (fc.name == kToolChangeSettings)
			return change_settings(fc.arguments, proposal);
		if (fc.name == kToolAddItem)
			return add_item(fc.arguments, proposal);
		return "Unknown function: " + fc.name;
	}

private:
	std::string change_settings(const std::string &arguments, json &proposal)
	{
		proposal = {{"tool", kToolChangeSettings}, {"arguments", json::parse(arguments, nullptr, false)}};
		if (proposal["arguments"].is_discarded())
			proposal["arguments"] = arguments;
		const Parsed<ChangeRequest> parsed = parse_change_settings(arguments);
		if (!parsed.ok) {
			proposal["result"] = "invalid";
			proposal["detail"] = parsed.error;
			return "Not applied. Invalid arguments: " + parsed.error;
		}
		std::vector<std::string> guide_only;
		bool output_change = false;
		bool video_change = false;
		for (const auto &change : parsed.value.changes) {
			if (change.target == ChangeTarget::OutputSetting ||
			    change.target == ChangeTarget::VideoSetting) {
				output_change = true;
				video_change = video_change || change.target == ChangeTarget::VideoSetting;
				continue;
			}
			if (!change.source.empty() && !sources_.count(change.source)) {
				proposal["result"] = "invalid";
				proposal["detail"] = "source not found: " + change.source;
				return "Not applied. Source not found: " + change.source;
			}
			if (!change.scene.empty() && !scenes_.count(change.scene)) {
				proposal["result"] = "invalid";
				proposal["detail"] = "scene not found: " + change.scene;
				return "Not applied. Scene not found: " + change.scene;
			}
		}
		if (output_change && outputs_active_) {
			proposal["result"] = "blocked_active";
			return "Not applied. Output and video settings cannot be changed while streaming, recording, the "
			       "replay buffer or the virtual camera is active. Ask the user to stop them first.";
		}
		for (const auto &change : parsed.value.changes) {
			if (change.target != ChangeTarget::OutputSetting && change.target != ChangeTarget::VideoSetting)
				continue;
			const auto old_it = output_settings_.find(change.key);
			const std::string old_value = old_it != output_settings_.end() ? old_it->second : std::string();
			if (classify_output_setting(mode_, change.key, old_value, value_as_text(change.value)) ==
			    OutputSettingClass::GuideOnly)
				guide_only.push_back(change.key);
		}
		if (!guide_only.empty()) {
			std::string keys;
			for (const auto &key : guide_only)
				keys += (keys.empty() ? "" : ", ") + key;
			proposal["result"] = "guide_only";
			proposal["detail"] = keys;
			return "Not applied. These items cannot be applied by this tool: " + keys +
			       ". Explain the steps in the OBS settings window instead (change it in Settings and press OK "
			       "while all outputs are stopped).";
		}
		for (const auto &change : parsed.value.changes)
			apply_change(change);
		proposal["result"] = "applied";
		std::string result = "The user approved the proposal and the changes were applied.";
		if (video_change)
			result += " The video settings were reset and the new resolution and FPS are active.";
		else if (output_change)
			result += " Output settings take effect the next time streaming or recording starts.";
		return result;
	}

	std::string add_item(const std::string &arguments, json &proposal)
	{
		proposal = {{"tool", kToolAddItem}, {"arguments", json::parse(arguments, nullptr, false)}};
		if (proposal["arguments"].is_discarded())
			proposal["arguments"] = arguments;
		const Parsed<AddRequest> parsed = parse_add_item(arguments);
		if (!parsed.ok) {
			proposal["result"] = "invalid";
			proposal["detail"] = parsed.error;
			return "Not added. Invalid arguments: " + parsed.error;
		}
		if (parsed.value.kind->is_filter && !sources_.count(parsed.value.source)) {
			proposal["result"] = "invalid";
			proposal["detail"] = "source not found: " + parsed.value.source;
			return "Not added. Source not found: " + parsed.value.source;
		}
		if (!parsed.value.kind->is_filter && !scenes_.count(parsed.value.scene)) {
			proposal["result"] = "invalid";
			proposal["detail"] = "scene not found: " + parsed.value.scene;
			return "Not added. Scene not found: " + parsed.value.scene;
		}
		apply_add(parsed.value);
		proposal["result"] = "applied";
		return "The user approved the proposal and the item was added.";
	}

	// 適用した変更を、以後の取得の道具の結果に映す。映さないと、モデルが同じ提案を繰り返す
	void apply_change(const Change &change)
	{
		json &details = results_["get_source_details"];
		json &lists = results_["list_scenes_and_sources"];
		const auto audio_field = [&](const char *field, const json &value) {
			for (auto &audio : lists["audio_sources"]) {
				if (audio.value("name", std::string()) == change.source)
					audio[field] = value;
			}
		};
		switch (change.target) {
		case ChangeTarget::SourceSetting:
			if (details.contains(change.source))
				details[change.source]["settings"][change.key] = change.value;
			break;
		case ChangeTarget::FilterSetting:
		case ChangeTarget::FilterEnabled:
			if (!details.contains(change.source))
				break;
			for (auto &filter : details[change.source]["filters"]) {
				if (filter.value("name", std::string()) != change.filter)
					continue;
				if (change.target == ChangeTarget::FilterEnabled)
					filter["enabled"] = change.value;
				else
					filter["settings"][change.key] = change.value;
			}
			break;
		case ChangeTarget::SourceVisibility:
			for (auto &scene : lists["scenes"]) {
				if (scene.value("name", std::string()) != change.scene)
					continue;
				for (auto &item : scene["items"]) {
					if (item.value("source", std::string()) == change.source)
						item["visible"] = change.value;
				}
			}
			break;
		case ChangeTarget::AudioMute:
			audio_field("muted", change.value);
			break;
		case ChangeTarget::AudioVolumeDb:
			audio_field("volume_db", change.value);
			break;
		case ChangeTarget::AudioMonitoring:
			audio_field("monitoring", change.value);
			break;
		case ChangeTarget::AudioSyncOffsetMs:
			audio_field("sync_offset_ms", change.value);
			break;
		case ChangeTarget::AudioTracks:
			audio_field("tracks", change.value);
			break;
		case ChangeTarget::OutputSetting:
		case ChangeTarget::VideoSetting:
			output_settings_[change.key] = value_as_text(change.value);
			if (results_.contains("get_obs_status"))
				results_["get_obs_status"]["output"]["settings"][change.key] = change.value;
			break;
		}
	}

	void apply_add(const AddRequest &request)
	{
		const json item = {{"name", request.name},
				   {"type", request.kind->obs_id},
				   {"enabled", true},
				   {"settings", request.settings}};
		if (request.kind->is_filter) {
			json &details = results_["get_source_details"];
			if (!details.contains(request.source))
				details[request.source] = {{"name", request.source}, {"filters", json::array()}};
			details[request.source]["filters"].push_back(item);
			return;
		}
		sources_.insert(request.name);
		json &lists = results_["list_scenes_and_sources"];
		for (auto &scene : lists["scenes"]) {
			if (scene.value("name", std::string()) == request.scene)
				scene["items"].push_back(
					{{"source", request.name}, {"type", request.kind->obs_id}, {"visible", true}});
		}
		if (request.kind->kind == AddKind::ApplicationAudioCapture)
			lists["audio_sources"].push_back({{"name", request.name},
							  {"type", request.kind->obs_id},
							  {"volume_db", 0.0},
							  {"muted", false},
							  {"monitoring", "off"},
							  {"sync_offset_ms", 0},
							  {"tracks", json::array({1, 2, 3, 4, 5, 6})}});
	}

	json scenario_;
	json results_;
	bool outputs_active_ = false;
	OutputMode mode_ = OutputMode::Simple;
	std::map<std::string, std::string> output_settings_;
	std::set<std::string> scenes_;
	std::set<std::string> sources_;
};

json run_scenario(const OpenAiClient &client, const EvalConfig &config, const Scenario &scenario, double &spent)
{
	const Price price = price_for(config.model);
	ModelConfig model_config;
	model_config.model = config.model;
	model_config.reasoning_effort = config.effort;

	json tools = build_tools_json();
	if (!config.web_search) {
		json filtered = json::array();
		for (const auto &tool : tools) {
			if (tool.value("type", std::string()) != "web_search")
				filtered.push_back(tool);
		}
		tools = filtered;
	}

	MockObs obs(scenario.data);
	const json replies = scenario.data.value("user_replies", json::array());
	const int stop_after_reply = scenario.data.value("outputs_stop_after_reply", -1);

	json record = {{"config",
			{{"label", config.label()},
			 {"model", config.model},
			 {"effort", config.effort},
			 {"web_search", config.web_search}}},
		       {"scenario", scenario.id},
		       {"title", scenario.data.value("title", scenario.id)},
		       {"events", json::array()},
		       {"requests", json::array()},
		       {"turns", json::array()},
		       {"proposals", json::array()},
		       {"tool_calls", json::array()}};

	std::vector<json> input = {user_message_item(scenario.data.value("first_message", std::string()))};
	record["events"].push_back({{"type", "user"}, {"text", scenario.data.value("first_message", std::string())}});

	size_t reply_index = 0;
	int turn = 0;
	double turn_latency = 0.0;
	int turn_requests = 0;
	std::string end_reason = "request_limit";

	for (int index = 0; index < kMaxRequestsPerScenario; ++index) {
		// プラグインと同じく、共通の指示文に回答の言語の指示を足す。試験の台本は日本語の利用者を想定する
		const json body = build_request(
			model_config, assistant_instructions() + "\n\n" + language_instruction("ja-JP"), tools, input);
		HttpResult http;
		// 送り直す前の失敗。利用者が待つ時間と、送り直しの頻度を後から数えるため残す
		json failed_attempts = json::array();
		for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
			http = client.post_responses(body);
			const bool retry = http.transport_error.empty()
						   ? (http.http_status == 429 || http.http_status >= 500)
						   : true;
			if (!retry || attempt == kMaxRetries)
				break;
			failed_attempts.push_back({{"elapsed_ms", std::round(http.elapsed_ms)},
						   {"http_status", http.http_status},
						   {"transport_error", http.transport_error}});
			std::printf("  retry after http=%ld ms=%.0f %s\n", http.http_status, http.elapsed_ms,
				    http.transport_error.c_str());
			std::this_thread::sleep_for(std::chrono::seconds(5 * (attempt + 1)));
		}

		const ParsedResponse parsed = parse_response(http.http_status, http.body);
		const int searches = std::max(parsed.web_search_calls, parsed.usage.web_search_requests);
		const double cost = request_cost(price, parsed.usage, searches);
		spent += cost;
		turn_latency += http.elapsed_ms;
		++turn_requests;

		json request_record = {{"index", index},
				       {"turn", turn},
				       {"elapsed_ms", std::round(http.elapsed_ms)},
				       {"http_status", http.http_status},
				       {"status", parsed.status},
				       {"input_tokens", parsed.usage.input_tokens},
				       {"cached_tokens", parsed.usage.cached_tokens},
				       {"output_tokens", parsed.usage.output_tokens},
				       {"reasoning_tokens", parsed.usage.reasoning_tokens},
				       {"web_search_calls", parsed.web_search_calls},
				       {"web_search_requests", parsed.usage.web_search_requests},
				       {"cost_usd", cost}};
		if (!parsed.incomplete_reason.empty())
			request_record["incomplete_reason"] = parsed.incomplete_reason;
		if (!failed_attempts.empty())
			request_record["failed_attempts"] = failed_attempts;
		std::printf("  %s req=%d http=%ld status=%s ms=%.0f in=%lld cached=%lld out=%lld search=%d cost=%.4f\n",
			    scenario.id.c_str(), index, http.http_status, parsed.status.c_str(), http.elapsed_ms,
			    parsed.usage.input_tokens, parsed.usage.cached_tokens, parsed.usage.output_tokens, searches,
			    cost);
		std::fflush(stdout);

		if (!http.transport_error.empty() || !parsed.ok) {
			json error = {{"type", "error"},
				      {"http_status", http.http_status},
				      {"transport_error", http.transport_error},
				      {"error_type", parsed.error_type},
				      {"error_code", parsed.error_code},
				      {"error_message", parsed.error_message}};
			request_record["error"] = error;
			record["requests"].push_back(request_record);
			record["events"].push_back(error);
			std::printf("  error: %s %s %s\n", parsed.error_type.c_str(), parsed.error_code.c_str(),
				    parsed.error_message.c_str());
			end_reason = "api_error";
			break;
		}
		record["requests"].push_back(request_record);

		for (const auto &item : parsed.output_items) {
			input.push_back(item);
			const std::string type = item.value("type", std::string());
			if (type == "web_search_call") {
				record["events"].push_back(
					{{"type", "web_search"},
					 {"request", index},
					 {"action", without_secrets(item.value("action", json::object()))}});
			} else if (type == "reasoning") {
				record["events"].push_back({{"type", "reasoning"}, {"request", index}});
			}
		}

		if (!parsed.function_calls.empty()) {
			for (const auto &fc : parsed.function_calls) {
				json proposal;
				const std::string output = obs.call(fc, proposal);
				input.push_back(function_call_output_item(fc.call_id, output));
				std::string call_name = fc.name;
				if (fc.name == kToolGetSourceDetails) {
					const json args = json::parse(fc.arguments, nullptr, false);
					if (args.is_object())
						call_name += ":" + args.value("source", std::string());
				}
				record["tool_calls"].push_back(call_name);
				const bool is_read = find_function_tool(fc.name) &&
						     find_function_tool(fc.name)->kind == ToolKind::Read;
				json event = {{"type", "function_call"},
					      {"request", index},
					      {"name", fc.name},
					      {"arguments", fc.arguments},
					      {"output_bytes", output.size()}};
				const bool is_error = output.rfind("{\"error\"", 0) == 0;
				if (!is_read || is_error)
					event["output"] = output;
				record["events"].push_back(event);
				if (!proposal.is_null()) {
					proposal["request"] = index;
					record["proposals"].push_back(proposal);
				}
			}
			continue;
		}

		json citations = json::array();
		for (const auto &citation : parsed.citations)
			citations.push_back(citation.url);
		record["events"].push_back({{"type", "assistant"},
					    {"request", index},
					    {"text", parsed.text},
					    {"citations", citations},
					    {"status", parsed.status}});
		record["turns"].push_back(
			{{"turn", turn}, {"latency_ms", std::round(turn_latency)}, {"requests", turn_requests}});
		turn_latency = 0.0;
		turn_requests = 0;

		if (reply_index >= replies.size()) {
			end_reason = "replies_exhausted";
			break;
		}
		if (static_cast<int>(reply_index) == stop_after_reply)
			obs.stop_outputs();
		obs.change_before_reply(reply_index);
		const std::string reply = replies[reply_index++].get<std::string>();
		input.push_back(user_message_item(reply));
		record["events"].push_back({{"type", "user"}, {"text", reply}});
		++turn;
	}

	record["end_reason"] = end_reason;
	return record;
}

int command_run(int argc, char **argv)
{
	if (argc < 5) {
		std::fprintf(stderr,
			     "usage: aia-model-eval run <scenarios> <out> <model:effort[:noweb]> [--only prefix] "
			     "[--budget usd] [--env path]\n");
		return 2;
	}
	const fs::path scenarios_dir = argv[2];
	const fs::path out_dir = argv[3];
	EvalConfig config;
	if (!parse_config(argv[4], config)) {
		std::fprintf(stderr, "unknown config: %s\n", argv[4]);
		return 2;
	}
	std::string only;
	double budget = 5.0;
	fs::path env_path = kDefaultEnvPath;
	for (int i = 5; i + 1 < argc; i += 2) {
		if (std::strcmp(argv[i], "--only") == 0)
			only = argv[i + 1];
		else if (std::strcmp(argv[i], "--budget") == 0)
			budget = std::atof(argv[i + 1]);
		else if (std::strcmp(argv[i], "--env") == 0)
			env_path = argv[i + 1];
	}

	const std::string key = read_api_key(env_path);
	if (key.empty()) {
		std::fprintf(stderr, "OPENAI_API_KEY was not found in the env file\n");
		return 1;
	}

	fs::create_directories(out_dir / "runs");
	curl_global_init(CURL_GLOBAL_DEFAULT);
	const OpenAiClient client(key);

	double spent = 0.0;
	int exit_code = 0;
	for (const auto &scenario : load_scenarios(scenarios_dir)) {
		if (!only.empty() && scenario.id.rfind(only, 0) != 0)
			continue;
		if (spent >= budget) {
			std::printf("budget reached (%.4f >= %.2f), stopping\n", spent, budget);
			exit_code = 3;
			break;
		}
		std::printf("[%s] %s\n", config.label().c_str(), scenario.id.c_str());
		std::fflush(stdout);
		const json record = run_scenario(client, config, scenario, spent);
		write_file(out_dir / "runs" / (config.label() + "__" + scenario.id + ".json"), record.dump(1));
		std::printf("  end=%s spent=%.4f\n", record["end_reason"].get<std::string>().c_str(), spent);
		std::fflush(stdout);
	}
	curl_global_cleanup();
	return exit_code;
}

// ---- 集計 ----

double percentile(std::vector<double> values, double p)
{
	if (values.empty())
		return NAN;
	std::sort(values.begin(), values.end());
	const size_t rank = static_cast<size_t>(std::ceil(p * static_cast<double>(values.size())));
	return values[std::min(values.size(), std::max<size_t>(rank, 1)) - 1];
}

double median(std::vector<double> values)
{
	if (values.empty())
		return NAN;
	std::sort(values.begin(), values.end());
	const size_t n = values.size();
	return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

std::string fmt(const char *format, double value)
{
	if (std::isnan(value))
		return "-";
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), format, value);
	return buffer;
}

std::string with_commas(long long value)
{
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
		digits.insert(static_cast<size_t>(i), ",");
	return (value < 0 ? "-" : "") + digits;
}

std::string assistant_text(const json &run)
{
	std::string text;
	for (const auto &event : run["events"]) {
		if (event["type"] == "assistant")
			text += event.value("text", std::string()) + "\n";
	}
	return text;
}

bool contains_any(const std::string &text, const json &words)
{
	for (const auto &word : words) {
		if (text.find(word.get<std::string>()) != std::string::npos)
			return true;
	}
	return false;
}

bool tool_called(const json &run, const std::string &name)
{
	for (const auto &call : run["tool_calls"]) {
		if (call.get<std::string>() == name)
			return true;
	}
	return false;
}

bool proposal_matches(const json &proposal, const json &pattern)
{
	if (proposal.value("tool", std::string()) != pattern.value("tool", std::string()))
		return false;
	const json &args = proposal["arguments"];
	if (!args.is_object())
		return false;
	if (pattern["tool"] == kToolAddItem) {
		if (pattern.contains("kind") && args.value("kind", std::string()) != pattern["kind"])
			return false;
		if (pattern.contains("source") && args.value("source", json()) != pattern["source"])
			return false;
		return true;
	}
	for (const auto &change : args.value("changes", json::array())) {
		if (!change.is_object())
			continue;
		if (pattern.contains("target") && change.value("target", std::string()) != pattern["target"])
			continue;
		if (pattern.contains("source") && change.value("source", json()) != pattern["source"])
			continue;
		if (pattern.contains("key") && change.value("key", json()) != pattern["key"])
			continue;
		return true;
	}
	return false;
}

struct Judgement {
	bool cause_hit = false;
	bool read_ok = false;
	bool fix_proposed = false;
	int forbidden = 0;
	int guide_only = 0;
	int blocked_active = 0;
	int invalid = 0;
	bool guide_text = true;
	// judge の forbid_tools が真の場面 (関係ない話題) で、OBS の道具も Web 検索も使わなかった
	bool no_tools = true;
	std::vector<std::string> missing_reads;
};

Judgement judge(const json &run, const json &scenario)
{
	Judgement result;
	const json criteria = scenario.value("judge", json::object());
	const std::string text = assistant_text(run);
	result.cause_hit = true;
	for (const auto &group : criteria.value("cause_keywords", json::array()))
		result.cause_hit = result.cause_hit && contains_any(text, group);
	result.read_ok = true;
	for (const auto &group : criteria.value("must_read", json::array())) {
		bool any = false;
		for (const auto &name : group)
			any = any || tool_called(run, name.get<std::string>());
		if (!any) {
			result.read_ok = false;
			result.missing_reads.push_back(group.dump());
		}
	}
	for (const auto &proposal : run["proposals"]) {
		const std::string outcome = proposal.value("result", std::string());
		if (outcome == "guide_only")
			++result.guide_only;
		else if (outcome == "blocked_active")
			++result.blocked_active;
		else if (outcome == "invalid")
			++result.invalid;
		for (const auto &pattern : criteria.value("good_proposals", json::array())) {
			if (outcome != "invalid" && proposal_matches(proposal, pattern))
				result.fix_proposed = true;
		}
		for (const auto &pattern : criteria.value("forbidden_proposals", json::array())) {
			if (proposal_matches(proposal, pattern))
				++result.forbidden;
		}
	}
	if (criteria.contains("expected_guide_text"))
		result.guide_text = contains_any(text, criteria["expected_guide_text"]);
	if (criteria.value("forbid_tools", false)) {
		result.no_tools = run["tool_calls"].empty();
		for (const auto &event : run["events"]) {
			if (event["type"] == "web_search")
				result.no_tools = false;
		}
	}
	return result;
}

struct ConfigStats {
	std::string label;
	std::string model;
	bool web_search = true;
	std::vector<double> turn_latency;
	std::vector<double> request_latency;
	std::vector<double> scenario_cost;
	int scenarios = 0;
	int cause_hit = 0;
	int read_ok = 0;
	int fix_proposed = 0;
	int forbidden = 0;
	int guide_only = 0;
	int blocked_active = 0;
	int invalid = 0;
	int guide_text_ok = 0;
	int guide_text_total = 0;
	int requests = 0;
	int errors = 0;
	int retries = 0;
	int searches = 0;
	int incomplete = 0;
	long long input_tokens = 0;
	long long cached_tokens = 0;
	long long output_tokens = 0;
	long long reasoning_tokens = 0;
	// 検索した要求の input_tokens の増分と、検索しなかった要求の増分
	std::vector<double> search_jumps;
	std::vector<double> plain_jumps;
};

std::string md_quote(const std::string &text)
{
	std::string out = "> ";
	for (const char c : text) {
		out += c;
		if (c == '\n')
			out += "> ";
	}
	return out;
}

void render_run(std::ostringstream &out, const json &run)
{
	for (const auto &event : run["events"]) {
		const std::string type = event["type"];
		if (type == "user") {
			// 直前の道具の箇条書きと段落を分けるため、空行を先に置く
			out << "\n利用者:\n\n" << md_quote(event["text"].get<std::string>()) << "\n\n";
		} else if (type == "assistant") {
			out << "\nアシスタント (要求 " << event["request"].get<int>() << "):\n\n"
			    << md_quote(event["text"].get<std::string>()) << "\n\n";
			if (!event["citations"].empty()) {
				out << "出典:";
				for (const auto &url : event["citations"])
					out << " " << url.get<std::string>();
				out << "\n\n";
			}
		} else if (type == "function_call") {
			out << "- 道具 (要求 " << event["request"].get<int>() << "): `"
			    << event["name"].get<std::string>() << "` 引数 `" << event["arguments"].get<std::string>()
			    << "`";
			if (event.contains("output"))
				out << " 結果: `" << event["output"].get<std::string>() << "`";
			else
				out << " 結果: 場面のファイルの値 (" << event["output_bytes"].get<long long>()
				    << " バイト)";
			out << "\n";
		} else if (type == "web_search") {
			const json action = event["action"];
			out << "- Web 検索 (要求 " << event["request"].get<int>() << "): ";
			if (action.contains("query"))
				out << "`" << action["query"].get<std::string>() << "`";
			else if (action.contains("queries"))
				out << "`" << action["queries"].dump() << "`";
			else
				out << "`" << action.value("type", std::string()) << "`";
			if (action.contains("sources")) {
				out << " 参照 " << action["sources"].size() << " 件";
				int shown = 0;
				for (const auto &source : action["sources"]) {
					if (shown++ >= 5)
						break;
					out << " " << source.value("url", std::string());
				}
			}
			out << "\n";
		} else if (type == "error") {
			out << "- エラー: `" << event.dump() << "`\n";
		}
	}
	out << "\n終わり方: `" << run.value("end_reason", std::string()) << "`\n\n";
	out << "| 要求 | 時間 (ms) | input | cached | output | reasoning | 検索 | 費用 (米ドル) | 送り直しの前の失敗 |\n";
	out << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
	for (const auto &request : run["requests"]) {
		std::string failures;
		for (const auto &failure : request.value("failed_attempts", json::array()))
			failures += (failures.empty() ? "" : ", ") +
				    with_commas(failure["elapsed_ms"].get<long long>()) + " ms " +
				    failure["transport_error"].get<std::string>();
		out << "| " << request["index"].get<int>() << " | "
		    << with_commas(request["elapsed_ms"].get<long long>()) << " | "
		    << with_commas(request["input_tokens"].get<long long>()) << " | "
		    << with_commas(request["cached_tokens"].get<long long>()) << " | "
		    << with_commas(request["output_tokens"].get<long long>()) << " | "
		    << with_commas(request["reasoning_tokens"].get<long long>()) << " | "
		    << std::max(request["web_search_calls"].get<int>(), request["web_search_requests"].get<int>())
		    << " | " << fmt("%.4f", request["cost_usd"].get<double>()) << " | " << failures << " |\n";
	}
	out << "\n";
}

int command_report(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: aia-model-eval report <scenarios> <out>\n");
		return 2;
	}
	const fs::path out_dir = argv[3];
	std::map<std::string, json> scenarios;
	std::vector<std::string> scenario_order;
	for (const auto &scenario : load_scenarios(argv[2])) {
		scenarios[scenario.id] = scenario.data;
		scenario_order.push_back(scenario.id);
	}

	// 構成の並びは引数の順に固定せず、名前の順にする
	std::map<std::string, std::map<std::string, json>> runs;
	for (const auto &entry : fs::directory_iterator(out_dir / "runs")) {
		if (entry.path().extension() != ".json")
			continue;
		const json run = json::parse(read_file(entry.path()), nullptr, false);
		if (run.is_discarded())
			continue;
		runs[run["config"]["label"].get<std::string>()][run["scenario"].get<std::string>()] = run;
	}

	std::ostringstream outputs;
	outputs << "# モデルの比較の会話の全文\n\n";
	outputs << "`aia-model-eval report` が `runs/*.json` から生成した。道具の結果のうち取得の道具の値は "
		   "`tools/model-eval/scenarios/` の場面のファイルにあるため、ここでは大きさだけを示す。"
		   "推論のアイテムの暗号化された中身は載せない。\n\n";

	std::map<std::string, ConfigStats> stats;
	std::map<std::string, std::map<std::string, Judgement>> judgements;
	for (const auto &scenario_id : scenario_order) {
		outputs << "## " << scenario_id << ": " << scenarios[scenario_id].value("title", std::string())
			<< "\n\n";
		for (auto &[label, by_scenario] : runs) {
			const auto it = by_scenario.find(scenario_id);
			if (it == by_scenario.end())
				continue;
			const json &run = it->second;
			outputs << "### " << label << "\n\n";
			render_run(outputs, run);

			ConfigStats &s = stats[label];
			s.label = label;
			s.model = run["config"]["model"];
			s.web_search = run["config"]["web_search"];
			++s.scenarios;
			const Judgement j = judge(run, scenarios[scenario_id]);
			judgements[label][scenario_id] = j;
			s.cause_hit += j.cause_hit;
			s.read_ok += j.read_ok;
			s.fix_proposed += j.fix_proposed;
			s.forbidden += j.forbidden;
			s.guide_only += j.guide_only;
			s.blocked_active += j.blocked_active;
			s.invalid += j.invalid;
			if (scenarios[scenario_id]["judge"].contains("expected_guide_text")) {
				++s.guide_text_total;
				s.guide_text_ok += j.guide_text;
			}
			double cost = 0.0;
			long long previous_input = -1;
			for (const auto &request : run["requests"]) {
				++s.requests;
				if (request.contains("error"))
					++s.errors;
				s.retries += static_cast<int>(request.value("failed_attempts", json::array()).size());
				if (request.value("status", std::string()) == "incomplete")
					++s.incomplete;
				const int searches = std::max(request["web_search_calls"].get<int>(),
							      request["web_search_requests"].get<int>());
				s.searches += searches;
				const long long input = request["input_tokens"].get<long long>();
				s.input_tokens += std::max(0LL, input);
				s.cached_tokens += std::max(0LL, request["cached_tokens"].get<long long>());
				s.output_tokens += std::max(0LL, request["output_tokens"].get<long long>());
				s.reasoning_tokens += std::max(0LL, request["reasoning_tokens"].get<long long>());
				s.request_latency.push_back(request["elapsed_ms"].get<double>());
				cost += request["cost_usd"].get<double>();
				if (previous_input >= 0 && input >= 0) {
					if (searches > 0)
						s.search_jumps.push_back(static_cast<double>(input - previous_input));
					else
						s.plain_jumps.push_back(static_cast<double>(input - previous_input));
				}
				previous_input = input;
			}
			for (const auto &turn : run["turns"])
				s.turn_latency.push_back(turn["latency_ms"].get<double>());
			s.scenario_cost.push_back(cost);
		}
	}
	write_file(out_dir / "outputs.md", outputs.str());

	std::ostringstream md;
	md << "# モデルの比較の集計\n\n";
	md << "`aia-model-eval report` が生成した。判定は場面のファイルの `judge` による自動の判定で、"
	      "会話を読んだ判断はレポートに書く。\n\n";
	md << "## 構成ごとの集計\n\n";
	md << "| 構成 | 場面 | 原因の言及 (自動) | 読むべき道具を読んだ | 直し方の提案 | 避けるべき提案 | 案内に回す項目の提案 | "
	      "稼働中で拒否 | 引数の誤り | 要求 | 送り直し | エラー | incomplete |\n";
	md << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
	for (const auto &[label, s] : stats) {
		md << "| " << label << " | " << s.scenarios << " | " << s.cause_hit << " | " << s.read_ok << " | "
		   << s.fix_proposed << " | " << s.forbidden << " | " << s.guide_only << " | " << s.blocked_active
		   << " | " << s.invalid << " | " << s.requests << " | " << s.retries << " | " << s.errors << " | "
		   << s.incomplete << " |\n";
	}
	md << "\n## 時間と費用\n\n";
	md << "応答までの時間は、利用者の発話を送ってから文章の返答を受け取るまでの要求の時間の合計。\n\n";
	md << "| 構成 | 応答の中央値 (ms) | 応答の p95 (ms) | 要求 1 回の中央値 (ms) | 1 回の相談の費用の平均 (米ドル) | "
	      "中央値 | 最大 | 合計 | input | cached | output | reasoning | Web 検索 |\n";
	md << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
	double grand_total = 0.0;
	for (const auto &[label, s] : stats) {
		double total = 0.0;
		for (const double c : s.scenario_cost)
			total += c;
		grand_total += total;
		md << "| " << label << " | " << with_commas(std::llround(median(s.turn_latency))) << " | "
		   << with_commas(std::llround(percentile(s.turn_latency, 0.95))) << " | "
		   << with_commas(std::llround(median(s.request_latency))) << " | "
		   << fmt("%.4f", total / std::max(1, s.scenarios)) << " | " << fmt("%.4f", median(s.scenario_cost))
		   << " | " << fmt("%.4f", *std::max_element(s.scenario_cost.begin(), s.scenario_cost.end())) << " | "
		   << fmt("%.4f", total) << " | " << with_commas(s.input_tokens) << " | "
		   << with_commas(s.cached_tokens) << " | " << with_commas(s.output_tokens) << " | "
		   << with_commas(s.reasoning_tokens) << " | " << s.searches << " |\n";
	}
	md << "\n全構成の費用の合計: " << fmt("%.4f", grand_total) << " 米ドル\n\n";

	md << "## Web 検索の取り込み量の見積もり\n\n";
	md << "### 方法 1: 同じ会話の中の input_tokens の増分\n\n";
	md << "直前の要求からの input_tokens の増分を、検索した要求と検索しなかった要求で分け、中央値の差を 1 回の検索の"
	      "取り込み量と見積もる。費用は取り込み量を入力の単価 (キャッシュなし) で計算し、検索の料金を足す。\n\n";
	md << "| 構成 | 検索した要求 | 増分の中央値 (検索あり) | 増分の中央値 (検索なし) | 1 回の取り込みの見積もり | "
	      "検索の料金 (米ドル) | 取り込みの費用 (米ドル) | 費用全体に占める割合 |\n";
	md << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
	for (const auto &[label, s] : stats) {
		if (!s.web_search)
			continue;
		const Price price = price_for(s.model);
		const double ingest = median(s.search_jumps) - median(s.plain_jumps);
		double total = 0.0;
		for (const double c : s.scenario_cost)
			total += c;
		const double fee = s.searches * kWebSearchFeeUsd;
		const double ingest_cost = std::isnan(ingest) ? 0.0
							      : std::max(0.0, ingest) * s.searches * price.input / 1e6;
		md << "| " << label << " | " << s.search_jumps.size() << " | "
		   << with_commas(std::llround(std::isnan(median(s.search_jumps)) ? 0 : median(s.search_jumps)))
		   << " | " << with_commas(std::llround(std::isnan(median(s.plain_jumps)) ? 0 : median(s.plain_jumps)))
		   << " | " << (std::isnan(ingest) ? std::string("-") : with_commas(std::llround(ingest))) << " | "
		   << fmt("%.4f", fee) << " | " << fmt("%.4f", ingest_cost) << " | "
		   << fmt("%.1f%%", total > 0 ? (fee + ingest_cost) / total * 100.0 : NAN) << " |\n";
	}

	md << "\n### 方法 2: Web 検索を外した構成との比較\n\n";
	md << "gpt-6.1-terra-low と gpt-6.1-terra-low-noweb の、場面ごとの最初の要求の input_tokens と、会話全体の "
	      "input_tokens の合計。会話の進み方が違うため、合計の差には検索以外の差も含まれる。\n\n";
	md << "| 場面 | 最初の要求 (検索あり) | 最初の要求 (検索なし) | 差 | 合計 (検索あり) | 合計 (検索なし) | 差 | "
	      "要求の数 (あり / なし) | 検索の回数 |\n";
	md << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
	const auto web_it = runs.find("gpt-6.1-terra-low");
	const auto noweb_it = runs.find("gpt-6.1-terra-low-noweb");
	if (web_it != runs.end() && noweb_it != runs.end()) {
		for (const auto &scenario_id : scenario_order) {
			const auto a = web_it->second.find(scenario_id);
			const auto b = noweb_it->second.find(scenario_id);
			if (a == web_it->second.end() || b == noweb_it->second.end())
				continue;
			const auto sum_input = [](const json &run) {
				long long total = 0;
				for (const auto &request : run["requests"])
					total += std::max(0LL, request["input_tokens"].get<long long>());
				return total;
			};
			const auto count_search = [](const json &run) {
				int total = 0;
				for (const auto &request : run["requests"])
					total += std::max(request["web_search_calls"].get<int>(),
							  request["web_search_requests"].get<int>());
				return total;
			};
			const long long first_a = a->second["requests"][0]["input_tokens"].get<long long>();
			const long long first_b = b->second["requests"][0]["input_tokens"].get<long long>();
			const long long total_a = sum_input(a->second);
			const long long total_b = sum_input(b->second);
			md << "| " << scenario_id << " | " << with_commas(first_a) << " | " << with_commas(first_b)
			   << " | " << with_commas(first_a - first_b) << " | " << with_commas(total_a) << " | "
			   << with_commas(total_b) << " | " << with_commas(total_a - total_b) << " | "
			   << a->second["requests"].size() << " / " << b->second["requests"].size() << " | "
			   << count_search(a->second) << " |\n";
		}
	}

	md << "\n## 場面ごとの自動の判定\n\n";
	md << "記号: C は原因の言及、R は読むべき道具を読んだ、F は直し方の提案、X は避けるべき提案の数、G は案内に回す項目の"
	      "提案の数、道具なしは関係ない話題の場面で OBS の道具も Web 検索も使わなかったこと。\n\n";
	md << "| 場面 |";
	for (const auto &[label, s] : stats)
		md << " " << label << " |";
	md << "\n| --- |";
	for (size_t i = 0; i < stats.size(); ++i)
		md << " --- |";
	md << "\n";
	for (const auto &scenario_id : scenario_order) {
		md << "| " << scenario_id << " |";
		for (const auto &[label, s] : stats) {
			const auto it = judgements[label].find(scenario_id);
			if (it == judgements[label].end()) {
				md << " - |";
				continue;
			}
			const Judgement &j = it->second;
			md << " C" << (j.cause_hit ? "o" : "x") << " R" << (j.read_ok ? "o" : "x") << " F"
			   << (j.fix_proposed ? "o" : "x") << " X" << j.forbidden << " G" << j.guide_only;
			if (scenarios[scenario_id]["judge"].contains("expected_guide_text"))
				md << " 案内" << (j.guide_text ? "o" : "x");
			if (scenarios[scenario_id]["judge"].value("forbid_tools", false))
				md << " 道具なし" << (j.no_tools ? "o" : "x");
			md << " |";
		}
		md << "\n";
	}
	write_file(out_dir / "stats.md", md.str());
	std::printf("total cost: %.4f USD\n", grand_total);
	return 0;
}

// ひらがなかカタカナを含むとき日本語とみなす
bool looks_japanese(const std::string &text)
{
	// UTF-8 で U+3040..U+30FF は E3 81 80 .. E3 83 BF
	for (size_t i = 0; i + 1 < text.size(); ++i) {
		const auto a = static_cast<unsigned char>(text[i]);
		const auto b = static_cast<unsigned char>(text[i + 1]);
		if (a == 0xE3 && b >= 0x81 && b <= 0x83)
			return true;
	}
	return false;
}

// 回答と要約の言語が、OBS の表示言語と利用者が書く言語の組み合わせに合うことを確かめる。
// 期待: 利用者が書いた言語で答える。OBS の表示言語と同じなら当然それに一致する
int command_language(int argc, char **argv)
{
	const fs::path env_path = argc >= 3 ? fs::path(argv[2]) : fs::path(".env");
	const std::string key = read_api_key(env_path);
	if (key.empty()) {
		std::fprintf(stderr, "OPENAI_API_KEY was not found in the env file\n");
		return 2;
	}
	struct Case {
		const char *locale;
		bool user_japanese;
		const char *user;
		const char *assistant;
	};
	const Case cases[] = {
		{"ja-JP", true, "配信で自分の声が二重に聞こえると言われました", "マイクの音声モニタリングの設定を確かめましょう。"},
		{"en-US", false, "Viewers say my voice sounds doubled on stream", "Let's check the audio monitoring of your mic."},
		{"ja-JP", false, "Viewers say my voice sounds doubled on stream", "Let's check the audio monitoring of your mic."},
		{"en-US", true, "配信で自分の声が二重に聞こえると言われました", "マイクの音声モニタリングの設定を確かめましょう。"},
	};
	const OpenAiClient client(key, nullptr);
	const ModelConfig config;
	const json no_tools = json::array();
	int mismatches = 0;
	for (const auto &c : cases) {
		const std::string instructions = assistant_instructions() + "\n\n" + language_instruction(c.locale);
		Conversation conversation;
		conversation.add_user_message(c.user);
		json reply_request = build_request(config, instructions, no_tools, conversation.request_items());
		const HttpResult reply = client.post_responses(reply_request);
		const ParsedResponse parsed_reply = parse_response(reply.http_status, reply.body);

		ParsedResponse assistant;
		assistant.ok = true;
		assistant.text = c.assistant;
		assistant.output_items.push_back({{"type", "message"},
						  {"role", "assistant"},
						  {"content", json::array({{{"type", "output_text"}, {"text", c.assistant}}})}});
		conversation.add_response(assistant);
		const HttpResult summary =
			client.post_responses(conversation.build_summary_request(config, instructions, no_tools));
		const ParsedResponse parsed_summary = parse_response(summary.http_status, summary.body);

		const bool reply_ok = parsed_reply.ok && looks_japanese(parsed_reply.text) == c.user_japanese;
		const bool summary_ok = parsed_summary.ok && looks_japanese(parsed_summary.text) == c.user_japanese;
		mismatches += (reply_ok ? 0 : 1) + (summary_ok ? 0 : 1);
		std::printf("== locale=%s user=%s reply=%s summary=%s\n-- reply: %s\n-- summary: %s\n", c.locale,
			    c.user_japanese ? "ja" : "en", reply_ok ? "OK" : "NG", summary_ok ? "OK" : "NG",
			    parsed_reply.text.c_str(), parsed_summary.text.c_str());
	}
	std::printf("mismatches=%d\n", mismatches);
	return mismatches == 0 ? 0 : 1;
}

// 応答に時間のかかる要求を、プラグインと同じ通信のコードで送り、かかった時間と失敗の理由を出す。
// 実機で 25 秒前後に "Timeout was reached" で切れた現象を切り分けるため
int command_slow(int argc, char **argv)
{
	const fs::path env_path = argc >= 3 ? fs::path(argv[2]) : fs::path(".env");
	const std::string key = read_api_key(env_path);
	if (key.empty()) {
		std::fprintf(stderr, "OPENAI_API_KEY was not found in the env file\n");
		return 2;
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	const OpenAiClient client(key, nullptr);
	ModelConfig config;
	config.max_output_tokens = 6000;
	const json input = json::array(
		{{{"role", "user"},
		  {"content", "Write the numbers from 1 to 700 in English words, one per line, with no other text."}}});
	const json request = build_request(config, "Follow the user's instruction exactly.", json::array(), input);
	if (argc >= 4 && std::strcmp(argv[3], "--trace") == 0) {
		// プラグインと同じ設定で送り、libcurl の状態の文だけを出す。送受信の中身 (認証のヘッダーを含む) は出さない
		CURL *curl = curl_easy_init();
		const std::string body = request.dump();
		std::string response;
		curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
		headers = curl_slist_append(headers, ("Authorization: Bearer " + key).c_str());
		curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/responses");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
				 +[](char *data, size_t size, size_t count, void *user) {
					 static_cast<std::string *>(user)->append(data, size * count);
					 return size * count;
				 });
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
		const std::string variant = argc >= 5 ? argv[4] : "";
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, variant == "connect180" ? 180L : 10L);
		if (variant == "tls12")
			curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION,
				 +[](CURL *, curl_infotype type, char *data, size_t size, void *) {
					 if (type == CURLINFO_TEXT)
						 std::fwrite(data, 1, size, stderr);
					 return 0;
				 });
		const auto started = std::chrono::steady_clock::now();
		const CURLcode rc = curl_easy_perform(curl);
		const double ms =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		std::printf("trace: rc=%d (%s) http=%ld elapsed_ms=%.0f bytes=%zu\n", static_cast<int>(rc),
			    curl_easy_strerror(rc), status, ms, response.size());
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return rc == CURLE_OK ? 0 : 1;
	}
	const HttpResult result = client.post_responses(request);
	const ParsedResponse parsed = parse_response(result.http_status, result.body);
	std::printf("http=%ld elapsed_ms=%.0f transport_error=\"%s\" status=%s output_tokens=%lld text_bytes=%zu\n",
		    result.http_status, result.elapsed_ms, result.transport_error.c_str(), parsed.status.c_str(),
		    parsed.usage.output_tokens, parsed.text.size());
	curl_global_cleanup();
	return result.transport_error.empty() ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc >= 2 && std::strcmp(argv[1], "run") == 0)
		return command_run(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "report") == 0)
		return command_report(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "slow") == 0)
		return command_slow(argc, argv);
	if (argc >= 2 && std::strcmp(argv[1], "language") == 0)
		return command_language(argc, argv);
	std::fprintf(stderr, "usage: aia-model-eval run|report|language ...\n");
	return 2;
}
