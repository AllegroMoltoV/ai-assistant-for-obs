// libobs に依存しない層の単体テスト。外部の API は呼ばない

#include "core/add-kinds.h"
#include "core/change-request.h"
#include "core/output-settings-policy.h"
#include "core/responses.h"
#include "core/tool-definitions.h"
#include "core/tool-output.h"

#include <cstdio>
#include <set>
#include <string>

using namespace aia;
using nlohmann::json;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                    \
	do {                                                                           \
		++g_checks;                                                            \
		if (!(cond)) {                                                         \
			++g_failures;                                                  \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
		}                                                                      \
	} while (0)

bool contains_word(const std::string &text, const char *word)
{
	return text.find(word) != std::string::npos;
}

// 削除の道具と、配信・録画の開始と停止の道具が無いこと
void test_no_forbidden_tools()
{
	const auto &tools = function_tools();
	CHECK(tools.size() == 6);
	for (const auto &tool : tools) {
		const std::string name = tool.name;
		CHECK(!contains_word(name, "delete"));
		CHECK(!contains_word(name, "remove"));
		CHECK(!contains_word(name, "start"));
		CHECK(!contains_word(name, "stop"));
	}

	int operations = 0;
	for (const auto &tool : tools) {
		if (tool.kind == ToolKind::Operation) {
			++operations;
			const std::string name = tool.name;
			CHECK(name == kToolChangeSettings || name == kToolAddItem);
		}
	}
	CHECK(operations == 2);

	// change_settings の対象にも削除や開始と停止が無いこと
	for (const auto &target : change_targets()) {
		const std::string name = target.name;
		CHECK(!contains_word(name, "delete"));
		CHECK(!contains_word(name, "remove"));
		CHECK(!contains_word(name, "start"));
		CHECK(!contains_word(name, "stop"));
	}
}

// tools 配列が Web 検索と 6 つの関数で、strict のスキーマの条件を満たすこと
void test_tools_json()
{
	const json tools = build_tools_json();
	CHECK(tools.is_array());
	CHECK(tools.size() == 7);
	CHECK(tools[0]["type"] == "web_search");

	std::set<std::string> names;
	for (size_t i = 1; i < tools.size(); ++i) {
		const json &tool = tools[i];
		CHECK(tool["type"] == "function");
		CHECK(tool["strict"] == true);
		names.insert(tool["name"].get<std::string>());
		const json &params = tool["parameters"];
		CHECK(params["additionalProperties"] == false);
		CHECK(params["required"].size() == params["properties"].size());
		CHECK(find_function_tool(tool["name"].get<std::string>()) != nullptr);
	}
	CHECK(names.size() == 6);

	// add_item の kind の候補が決めた 6 つと一致すること
	for (const auto &tool : tools) {
		if (tool.value("name", "") != kToolAddItem)
			continue;
		const json &kinds = tool["parameters"]["properties"]["kind"]["enum"];
		std::set<std::string> listed;
		for (const auto &kind : kinds)
			listed.insert(kind.get<std::string>());
		const std::set<std::string> expected = {
			"noise_suppression",         "gain",          "compressor", "crop",
			"application_audio_capture", "browser_source"};
		CHECK(listed == expected);
		// 入れ子のオブジェクトにも strict の条件がかかる
	}
	for (const auto &tool : tools) {
		if (tool.value("name", "") != kToolChangeSettings)
			continue;
		const json &item = tool["parameters"]["properties"]["changes"]["items"];
		CHECK(item["additionalProperties"] == false);
		CHECK(item["required"].size() == item["properties"].size());
	}
}

void test_add_kinds()
{
	CHECK(add_kinds().size() == 6);
	CHECK(std::string(find_add_kind("noise_suppression")->obs_id) == "noise_suppress_filter_v2");
	CHECK(std::string(find_add_kind("gain")->obs_id) == "gain_filter");
	CHECK(std::string(find_add_kind("compressor")->obs_id) == "compressor_filter");
	CHECK(std::string(find_add_kind("crop")->obs_id) == "crop_filter");
	CHECK(std::string(find_add_kind("application_audio_capture")->obs_id) == "wasapi_process_output_capture");
	CHECK(std::string(find_add_kind("browser_source")->obs_id) == "browser_source");
	CHECK(find_add_kind("image_source") == nullptr);
	CHECK(find_add_kind("scene") == nullptr);
	CHECK(find_add_kind("") == nullptr);
}

void test_parse_add_item()
{
	auto ok = parse_add_item(
		R"({"reason":"r","kind":"noise_suppression","source":"Mic","scene":null,"name":"Noise","settings_json":"{}"})");
	CHECK(ok.ok);
	CHECK(ok.value.kind->kind == AddKind::NoiseSuppression);
	CHECK(ok.value.source == "Mic");

	auto browser = parse_add_item(
		R"({"reason":"r","kind":"browser_source","source":null,"scene":"Main","name":"Chat","settings_json":"{\"url\":\"https://example.com\"}"})");
	CHECK(browser.ok);
	CHECK(browser.value.settings["url"] == "https://example.com");

	// 決めた種類以外は追加できない
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"image_source","source":null,"scene":"Main","name":"x","settings_json":"{}"})")
		       .ok);
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"scene","source":null,"scene":"Main","name":"x","settings_json":"{}"})")
		       .ok);
	// フィルターにはソース、ソースにはシーンが要る
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"gain","source":null,"scene":"Main","name":"x","settings_json":"{}"})")
		       .ok);
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"browser_source","source":"Mic","scene":null,"name":"x","settings_json":"{}"})")
		       .ok);
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"gain","source":"Mic","scene":null,"name":"","settings_json":"{}"})")
		       .ok);
	CHECK(!parse_add_item(
		       R"({"reason":"r","kind":"gain","source":"Mic","scene":null,"name":"x","settings_json":"[1]"})")
		       .ok);
	CHECK(!parse_add_item("not json").ok);
}

std::string one_change(const char *target, const char *source, const char *value_json, const char *key = nullptr,
		       const char *filter = nullptr, const char *scene = nullptr)
{
	json change = {{"target", target},
		       {"scene", scene ? json(scene) : json(nullptr)},
		       {"source", source ? json(source) : json(nullptr)},
		       {"filter", filter ? json(filter) : json(nullptr)},
		       {"key", key ? json(key) : json(nullptr)},
		       {"value_json", value_json}};
	return json({{"reason", "r"}, {"changes", json::array({change})}}).dump();
}

void test_parse_change_settings()
{
	auto volume = parse_change_settings(one_change("audio_volume_db", "Mic", "-6.5"));
	CHECK(volume.ok);
	CHECK(volume.value.changes.size() == 1);
	CHECK(volume.value.changes[0].target == ChangeTarget::AudioVolumeDb);
	CHECK(volume.value.changes[0].value.get<double>() == -6.5);

	CHECK(!parse_change_settings(one_change("audio_volume_db", "Mic", "6")).ok);
	CHECK(!parse_change_settings(one_change("audio_volume_db", "Mic", "\"loud\"")).ok);
	CHECK(parse_change_settings(one_change("audio_monitoring", "Mic", "\"off\"")).ok);
	CHECK(!parse_change_settings(one_change("audio_monitoring", "Mic", "\"on\"")).ok);
	CHECK(parse_change_settings(one_change("audio_tracks", "Mic", "[1,2]")).ok);
	CHECK(!parse_change_settings(one_change("audio_tracks", "Mic", "[7]")).ok);
	CHECK(parse_change_settings(one_change("audio_sync_offset_ms", "Mic", "200")).ok);
	CHECK(!parse_change_settings(one_change("audio_sync_offset_ms", "Mic", "-2000")).ok);
	CHECK(!parse_change_settings(one_change("audio_sync_offset_ms", "Mic", "1.5")).ok);
	CHECK(parse_change_settings(one_change("audio_mute", "Mic", "true")).ok);
	CHECK(!parse_change_settings(one_change("audio_mute", "Mic", "1")).ok);

	// 対象に必要な名前が欠けていれば拒む
	CHECK(!parse_change_settings(one_change("audio_mute", nullptr, "true")).ok);
	CHECK(!parse_change_settings(one_change("source_visibility", "Game", "false")).ok);
	CHECK(parse_change_settings(one_change("source_visibility", "Game", "false", nullptr, nullptr, "Main")).ok);
	CHECK(!parse_change_settings(one_change("filter_setting", "Mic", "1", "db", nullptr)).ok);
	CHECK(parse_change_settings(one_change("filter_setting", "Mic", "1", "db", "Gain")).ok);
	CHECK(!parse_change_settings(one_change("output_setting", nullptr, "4500")).ok);
	CHECK(parse_change_settings(one_change("output_setting", nullptr, "4500", "SimpleOutput/VBitrate")).ok);

	// 一覧に無い対象、空の変更、壊れた JSON は拒む
	CHECK(!parse_change_settings(one_change("delete_source", "Mic", "true")).ok);
	CHECK(!parse_change_settings(R"({"reason":"r","changes":[]})").ok);
	CHECK(!parse_change_settings(one_change("audio_mute", "Mic", "tru")).ok);
	CHECK(!parse_change_settings("{").ok);
}

void test_output_settings_policy()
{
	const auto simple = OutputMode::Simple;
	const auto advanced = OutputMode::Advanced;
	const auto allowed = OutputSettingClass::Allowed;
	const auto guide = OutputSettingClass::GuideOnly;

	CHECK(classify_output_setting(simple, "SimpleOutput/VBitrate", "6000", "4500") == allowed);
	CHECK(classify_output_setting(simple, "SimpleOutput/ABitrate", "160", "128") == allowed);
	// エンコーダーの種類と録画の品質は手順の案内
	CHECK(classify_output_setting(simple, "SimpleOutput/StreamEncoder", "x264", "nvenc") == guide);
	CHECK(classify_output_setting(simple, "SimpleOutput/RecQuality", "Stream", "Small") == guide);
	CHECK(classify_output_setting(simple, "SimpleOutput/RecRB", "false", "true") == guide);
	CHECK(classify_output_setting(advanced, "AdvOut/Encoder", "obs_x264", "jim_nvenc") == guide);
	CHECK(classify_output_setting(advanced, "AdvOut/RecEncoder", "none", "obs_x264") == guide);
	CHECK(classify_output_setting(advanced, "AdvOut/TrackIndex", "1", "2") == guide);
	CHECK(classify_output_setting(advanced, "Output/Mode", "Advanced", "Simple") == guide);
	CHECK(classify_output_setting(advanced, "AdvOut/Track1Bitrate", "160", "192") == allowed);
	CHECK(classify_output_setting(advanced, "streamEncoder.json/bitrate", "6000", "4500") == allowed);
	CHECK(classify_output_setting(advanced, "recordEncoder.json/preset", "veryfast", "faster") == allowed);
	CHECK(classify_output_setting(advanced, "streamEncoder.json/", "", "") == guide);
	// エンコーダーの設定の JSON は詳細出力だけ
	CHECK(classify_output_setting(simple, "streamEncoder.json/bitrate", "6000", "4500") == guide);
	// モードの違う項目は書き換えない
	CHECK(classify_output_setting(simple, "AdvOut/Track1Bitrate", "160", "192") == guide);

	// 録画形式は同じ群の中だけ
	CHECK(classify_output_setting(advanced, "AdvOut/RecFormat2", "mkv", "mp4") == allowed);
	CHECK(classify_output_setting(advanced, "AdvOut/RecFormat2", "mkv", "hybrid_mp4") == guide);
	CHECK(classify_output_setting(advanced, "AdvOut/RecFormat2", "hybrid_mp4", "hybrid_mov") == guide);
	CHECK(classify_output_setting(simple, "SimpleOutput/RecFormat2", "mp4", "mov") == allowed);
	CHECK(classify_output_setting(simple, "AdvOut/RecFormat2", "mkv", "mp4") == guide);

	CHECK(classify_output_setting(simple, "Video/FPSCommon", "60", "30") == allowed);
	CHECK(classify_output_setting(advanced, "Video/OutputCX", "1920", "1280") == allowed);
	CHECK(is_video_setting_key("Video/BaseCY"));
	CHECK(!is_video_setting_key("Video/ColorFormat"));
	CHECK(classify_output_setting(simple, "Video/ColorFormat", "NV12", "P010") == guide);
	CHECK(classify_output_setting(simple, "Unknown/Key", "", "") == guide);
}

void test_build_request()
{
	ModelConfig config;
	std::vector<json> items = {user_message_item("hello")};
	const json request = build_request(config, "inst", build_tools_json(), items);
	CHECK(request["model"] == "gpt-6.1-terra");
	CHECK(request["store"] == false);
	CHECK(request["reasoning"]["effort"] == "low");
	CHECK(request["instructions"] == "inst");
	CHECK(request["tools"].size() == 7);
	CHECK(request["input"].size() == 1);
	CHECK(request["input"][0]["role"] == "user");
	bool has_encrypted = false;
	for (const auto &include : request["include"])
		has_encrypted = has_encrypted || include == "reasoning.encrypted_content";
	CHECK(has_encrypted);

	const json output = function_call_output_item("call_1", "{\"ok\":true}");
	CHECK(output["type"] == "function_call_output");
	CHECK(output["call_id"] == "call_1");
}

// docs/logs/2026-11-12-web-search-function-combo.md の応答の形を縮めたもの
void test_parse_response()
{
	const json first = json::parse(R"({
		"status": "completed",
		"output": [
			{"type": "function_call", "id": "fc_1", "call_id": "call_A", "name": "get_obs_status", "arguments": "{}", "status": "completed"}
		],
		"usage": {"input_tokens": 4623, "input_tokens_details": {"cached_tokens": 0}, "output_tokens": 15, "output_tokens_details": {"reasoning_tokens": 0}},
		"tool_usage": {"web_search": {"num_requests": 0}}
	})");
	const ParsedResponse a = parse_response(200, first);
	CHECK(a.ok);
	CHECK(a.function_calls.size() == 1);
	CHECK(a.function_calls[0].call_id == "call_A");
	CHECK(a.function_calls[0].name == "get_obs_status");
	CHECK(a.function_calls[0].arguments == "{}");
	CHECK(a.output_items.size() == 1);
	CHECK(a.usage.input_tokens == 4623);
	CHECK(a.text.empty());

	const json second = json::parse(R"({
		"status": "completed",
		"output": [
			{"type": "reasoning", "id": "rs_1", "encrypted_content": "xxx", "summary": []},
			{"type": "web_search_call", "id": "ws_1", "status": "completed", "action": {"type": "search", "query": "q"}},
			{"type": "message", "role": "assistant", "content": [
				{"type": "output_text", "text": "abc", "annotations": [
					{"type": "url_citation", "start_index": 1, "end_index": 3, "url": "https://obsproject.com/kb/a", "title": "A"}
				]},
				{"type": "output_text", "text": "defg", "annotations": [
					{"type": "url_citation", "start_index": 0, "end_index": 4, "url": "https://obsproject.com/kb/b", "title": "B"},
					{"type": "file_citation", "start_index": 0, "end_index": 1}
				]}
			]}
		],
		"usage": {"input_tokens": 13088, "input_tokens_details": {"cached_tokens": 4620}, "output_tokens": 531, "output_tokens_details": {"reasoning_tokens": 72}},
		"tool_usage": {"web_search": {"num_requests": 1}}
	})");
	const ParsedResponse b = parse_response(200, second);
	CHECK(b.ok);
	CHECK(b.function_calls.empty());
	// 推論のアイテムも次の要求へ返すため保持する
	CHECK(b.output_items.size() == 3);
	CHECK(b.output_items[0]["type"] == "reasoning");
	CHECK(b.web_search_calls == 1);
	CHECK(b.usage.web_search_requests == 1);
	CHECK(b.usage.cached_tokens == 4620);
	CHECK(b.usage.reasoning_tokens == 72);
	CHECK(b.text == "abcdefg");
	CHECK(b.citations.size() == 2);
	CHECK(b.citations[0].url == "https://obsproject.com/kb/a");
	CHECK(b.citations[0].start_index == 1);
	// 2 つ目の部分の出典の位置は、つないだ本文の位置にずらす
	CHECK(b.citations[1].start_index == 3);
	CHECK(b.citations[1].end_index == 7);

	const json incomplete = json::parse(
		R"({"status":"incomplete","incomplete_details":{"reason":"max_output_tokens"},"output":[]})");
	const ParsedResponse c = parse_response(200, incomplete);
	CHECK(c.ok);
	CHECK(c.incomplete_reason == "max_output_tokens");

	const json error = json::parse(
		R"({"error":{"type":"invalid_request_error","code":"invalid_api_key","message":"Incorrect API key"}})");
	const ParsedResponse d = parse_response(401, error);
	CHECK(!d.ok);
	CHECK(d.error_code == "invalid_api_key");

	CHECK(!parse_response(200, json::array()).ok);
}

void test_tool_output_limits()
{
	CHECK(limit_head("short", 10) == "short");
	CHECK(limit_tail("short", 10) == "short");

	// 「あ」は UTF-8 で 3 バイト。4 バイト目で切るときは文字の境界まで戻る
	const std::string kana = "あいう";
	const std::string head = limit_head(kana, 4);
	CHECK(head.rfind("あ", 0) == 0);
	CHECK(head.find("い") == std::string::npos);
	CHECK(contains_word(head, "[truncated"));

	const std::string log = "line1\nline2\nline3\nline4\n";
	const std::string tail = limit_tail(log, 10);
	CHECK(tail.rfind("[truncated", 0) == 0);
	CHECK(contains_word(tail, "line4"));
	CHECK(!contains_word(tail, "line1"));
	// 行の途中から始めない
	CHECK(contains_word(tail, "\nline"));
}

} // namespace

int main()
{
	test_no_forbidden_tools();
	test_tools_json();
	test_add_kinds();
	test_parse_add_item();
	test_parse_change_settings();
	test_output_settings_policy();
	test_build_request();
	test_parse_response();
	test_tool_output_limits();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
