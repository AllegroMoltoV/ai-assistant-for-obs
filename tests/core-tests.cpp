// libobs に依存しない層の単体テスト。外部の API は呼ばない

#include "core/add-kinds.h"
#include "core/change-request.h"
#include "core/conversation.h"
#include "core/display-names.h"
#include "core/moderation.h"
#include "core/obs-values.h"
#include "core/output-settings-policy.h"
#include "core/proposals.h"
#include "core/responses.h"
#include "core/secrets.h"
#include "core/session.h"
#include "core/tool-definitions.h"
#include "core/tool-output.h"

#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
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

// 配信・録画の開始と停止の道具が無く、削除の道具は AI が追加したものを消す remove_item の 1 つだけであること
void test_no_forbidden_tools()
{
	const auto &tools = function_tools();
	CHECK(tools.size() == 7);
	int removals = 0;
	for (const auto &tool : tools) {
		const std::string name = tool.name;
		if (contains_word(name, "delete") || contains_word(name, "remove")) {
			++removals;
			CHECK(name == kToolRemoveItem);
			CHECK(tool.kind == ToolKind::Operation);
		}
		CHECK(!contains_word(name, "start"));
		CHECK(!contains_word(name, "stop"));
	}
	CHECK(removals == 1);

	int operations = 0;
	for (const auto &tool : tools) {
		if (tool.kind == ToolKind::Operation) {
			++operations;
			const std::string name = tool.name;
			CHECK(name == kToolChangeSettings || name == kToolAddItem || name == kToolRemoveItem);
		}
	}
	CHECK(operations == 3);

	// 削除の対象はフィルタとソースだけで、シーンは選べない。説明でも追加したものに限ることを示す
	for (const auto &tool : build_tools_json()) {
		if (tool.value("name", "") != kToolRemoveItem)
			continue;
		CHECK(tool["parameters"]["properties"]["kind"]["enum"] == json::array({"filter", "source"}));
		CHECK(contains_word(tool["description"].get<std::string>(), "added with add_item"));
	}

	// change_settings の対象にも削除や開始と停止が無いこと
	for (const auto &target : change_targets()) {
		const std::string name = target.name;
		CHECK(!contains_word(name, "delete"));
		CHECK(!contains_word(name, "remove"));
		CHECK(!contains_word(name, "start"));
		CHECK(!contains_word(name, "stop"));
	}
}

// tools 配列が Web 検索と 7 つの関数で、strict のスキーマの条件を満たすこと
void test_tools_json()
{
	const json tools = build_tools_json();
	CHECK(tools.is_array());
	CHECK(tools.size() == 8);
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
	CHECK(names.size() == 7);

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

void test_parse_remove_item()
{
	auto filter = parse_remove_item(R"({"reason":"r","kind":"filter","source":"Mic","scene":null,"name":"Noise"})");
	CHECK(filter.ok);
	CHECK(filter.value.is_filter);
	CHECK(filter.value.source == "Mic");
	auto source = parse_remove_item(R"({"reason":"r","kind":"source","source":null,"scene":"Main","name":"Chat"})");
	CHECK(source.ok);
	CHECK(!source.value.is_filter);
	CHECK(source.value.scene == "Main");

	// シーンは削除できない
	CHECK(!parse_remove_item(R"({"reason":"r","kind":"scene","source":null,"scene":"Main","name":"Main"})").ok);
	// フィルタには付け先のソース、ソースにはシーンが要る
	CHECK(!parse_remove_item(R"({"reason":"r","kind":"filter","source":null,"scene":"Main","name":"x"})").ok);
	CHECK(!parse_remove_item(R"({"reason":"r","kind":"source","source":"Mic","scene":null,"name":"x"})").ok);
	CHECK(!parse_remove_item(R"({"reason":"r","kind":"filter","source":"Mic","scene":null,"name":""})").ok);
	CHECK(!parse_remove_item("not json").ok);
}

// 削除できるのは、追加したときに書いた目印があり、名前と付け先も一致するものだけ
void test_added_mark()
{
	const json filter_mark = added_mark("Noise", "Mic");
	CHECK(has_added_mark(filter_mark, "Noise", "Mic"));
	// 利用者が作ったものには目印が無い
	CHECK(!has_added_mark(json::object(), "Noise", "Mic"));
	CHECK(!has_added_mark(json(), "Noise", "Mic"));
	CHECK(!has_added_mark(json({{"other_plugin", true}}), "Noise", "Mic"));
	// 複製すると目印も写るが、名前か付け先が変わるため削除の対象にならない
	CHECK(!has_added_mark(filter_mark, "Noise 2", "Mic"));
	CHECK(!has_added_mark(filter_mark, "Noise", "Desktop Audio"));
	// 目印の値が偽や別の型なら AI が追加したものとして扱わない
	json wrong = filter_mark;
	wrong[kAddedMarkKey] = false;
	CHECK(!has_added_mark(wrong, "Noise", "Mic"));
	wrong[kAddedMarkKey] = "true";
	CHECK(!has_added_mark(wrong, "Noise", "Mic"));

	const json source_mark = added_mark("Chat", "");
	CHECK(has_added_mark(source_mark, "Chat", ""));
	CHECK(!has_added_mark(source_mark, "Chat 2", ""));
	// 目印のほかの private settings の値があっても判定は変わらない
	json mixed = source_mark;
	mixed["some_plugin_value"] = 1;
	CHECK(has_added_mark(mixed, "Chat", ""));
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
	// OBS 33 にはモニターだけの種類が無い
	CHECK(!parse_change_settings(one_change("audio_monitoring", "Mic", "\"monitor_only\"")).ok);
	CHECK(parse_change_settings(one_change("audio_monitoring", "Mic", "\"monitor_and_output\"")).ok);
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

// 反映の時期は docs/reports/2026-11-11-output-settings-apply.md の考察の「書き換えてよい項目」の表に従う
void test_output_apply_timing()
{
	const auto simple = OutputMode::Simple;
	const auto advanced = OutputMode::Advanced;
	CHECK(output_apply_timing(simple, "Video/FPSCommon", "60", "30") == ApplyTiming::VideoReset);
	CHECK(output_apply_timing(advanced, "Video/OutputCX", "1920", "1280") == ApplyTiming::VideoReset);
	CHECK(output_apply_timing(simple, "SimpleOutput/VBitrate", "6000", "4500") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(simple, "SimpleOutput/ABitrate", "160", "128") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(simple, "SimpleOutput/NVENCPreset2", "p5", "p4") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(simple, "SimpleOutput/RecTracks", "1", "3") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(simple, "SimpleOutput/VodTrackEnabled", "false", "true") ==
	      ApplyTiming::NextStreamStart);
	CHECK(output_apply_timing(simple, "SimpleOutput/RecFormat2", "mkv", "mp4") == ApplyTiming::NextRecordingStart);
	CHECK(output_apply_timing(simple, "SimpleOutput/RecFormat2", "mkv", "flv") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(advanced, "streamEncoder.json/bitrate", "6000", "4500") ==
	      ApplyTiming::NextStreamStart);
	CHECK(output_apply_timing(advanced, "recordEncoder.json/preset", "", "") == ApplyTiming::NextRecordingStart);
	CHECK(output_apply_timing(advanced, "AdvOut/Track1Bitrate", "160", "192") == ApplyTiming::NextStart);
	CHECK(output_apply_timing(advanced, "AdvOut/Track6Bitrate", "160", "192") == ApplyTiming::NextStart);
	CHECK(output_apply_timing(advanced, "AdvOut/RescaleRes", "1280x720", "1920x1080") ==
	      ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(advanced, "AdvOut/RecTracks", "1", "3") == ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(advanced, "AdvOut/StreamMultiTrackAudioMixes", "0", "1") ==
	      ApplyTiming::NextStreamStart);
	CHECK(output_apply_timing(advanced, "AdvOut/VodTrackEnabled", "false", "true") == ApplyTiming::NextStreamStart);
	CHECK(output_apply_timing(advanced, "AdvOut/RecFormat2", "mkv", "mp4") == ApplyTiming::NextRecordingStart);
	CHECK(output_apply_timing(advanced, "AdvOut/RecFormat2", "mkv", "fragmented_mp4") ==
	      ApplyTiming::NextStartAllStopped);
	CHECK(output_apply_timing(advanced, "AdvOut/RecFormat2", "flv", "mkv") == ApplyTiming::NextStartAllStopped);

	// 書き換えてよい項目は、すぐ反映されるもの以外に必ず承認の画面の一文がある
	for (const auto &key : writable_output_config_keys()) {
		const ApplyTiming timing =
			output_apply_timing(key.rfind("AdvOut/", 0) == 0 ? advanced : simple, key, "mkv", "mp4");
		CHECK(timing != ApplyTiming::Immediate);
		CHECK(apply_timing_notice_key(timing) != nullptr);
	}
	CHECK(apply_timing_notice_key(ApplyTiming::Immediate) == nullptr);
	CHECK(std::string(apply_timing_name(ApplyTiming::VideoReset)) == "immediately");
	CHECK(std::string(apply_timing_name(ApplyTiming::NextStreamStart)) == "next_stream_start");
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
	CHECK(request["tools"].size() == 8);
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

// 伏せる対象の 4 種類: 配信キー、配信の URL のキー、URL の問い合わせ文字列のトークン、項目名で判定する秘密の値
void test_mask_secrets()
{
	// 配信の URL のアプリ名より後ろ
	const std::string rtmp = mask_secrets_in_text("server rtmp://live.twitch.tv/app/live_123456_AbCdEf ok");
	CHECK(!contains_word(rtmp, "live_123456_AbCdEf"));
	CHECK(contains_word(rtmp, "rtmp://live.twitch.tv/app/"));
	const std::string rtmps =
		mask_secrets_in_text("rtmps://a.rtmps.youtube.com:443/live2/abcd-efgh-ijkl-mnop-qrst");
	CHECK(!contains_word(rtmps, "abcd-efgh"));

	// URL の問い合わせ文字列の値
	const std::string query =
		mask_secrets_in_text("https://streamlabs.com/alert-box/v3/x?token=ABCDEF123&lang=ja#frag");
	CHECK(!contains_word(query, "ABCDEF123"));
	CHECK(contains_word(query, "?token=***"));
	CHECK(contains_word(query, "&lang=***"));
	CHECK(contains_word(query, "https://streamlabs.com/alert-box/v3/x"));
	const std::string srt = mask_secrets_in_text("srt://example.com:9000?streamid=abc&passphrase=secret123");
	CHECK(!contains_word(srt, "secret123"));

	// 形で分かるキー
	CHECK(!contains_word(mask_secrets_in_text("key sk-proj-ABCDEFGHIJKLMNOPQRSTUV"), "ABCDEFGHIJ"));
	CHECK(!contains_word(mask_secrets_in_text("Authorization: Bearer abc.def.ghi"), "abc.def.ghi"));
	CHECK(!contains_word(mask_secrets_in_text("stream key live_987654_ZyXw"), "live_987654_ZyXw"));
	CHECK(!contains_word(mask_secrets_in_text("abcd-efgh-ijkl-mnop-qrst"), "abcd-efgh-ijkl-mnop-qrst"));

	// 秘密でない文はそのまま
	const std::string plain = "Output Resolution: 1920x1080, encoder obs_x264, keyint_sec 2";
	CHECK(mask_secrets_in_text(plain) == plain);

	// 項目名で判定する秘密の値
	CHECK(is_secret_field_name("key"));
	CHECK(is_secret_field_name("stream_key"));
	CHECK(is_secret_field_name("streamKey"));
	CHECK(is_secret_field_name("bearer_token"));
	CHECK(is_secret_field_name("password"));
	CHECK(!is_secret_field_name("keyint_sec"));
	CHECK(!is_secret_field_name("key_color_type"));
	CHECK(!is_secret_field_name("hotkey"));
	CHECK(!is_secret_field_name("bitrate"));

	const json settings = {
		{"service", "Twitch"},
		{"server", "rtmp://live.twitch.tv/app"},
		{"key", "live_111_aaa"},
		{"use_auth", true},
		{"password", "hunter2"},
		{"keyint_sec", 2},
		{"browser", {{"url", "https://overlay.example/x?token=zzz"}, {"css", "body{}"}}},
		{"list", json::array({"rtmp://h/app/secretkey", 1})},
	};
	const json masked = mask_secrets_in_json(settings);
	CHECK(masked["key"] == kMaskedValue);
	CHECK(masked["password"] == kMaskedValue);
	CHECK(masked["use_auth"] == true);
	CHECK(masked["keyint_sec"] == 2);
	CHECK(masked["service"] == "Twitch");
	CHECK(masked["server"] == "rtmp://live.twitch.tv/app");
	CHECK(!contains_word(masked["browser"]["url"].get<std::string>(), "zzz"));
	CHECK(!contains_word(masked["list"][0].get<std::string>(), "secretkey"));
	CHECK(!contains_word(masked.dump(), "hunter2"));
	CHECK(!contains_word(masked.dump(), "live_111_aaa"));
}

Change output_change(const char *key)
{
	Change change;
	change.target = ChangeTarget::OutputSetting;
	change.key = key;
	return change;
}

// 配信中・録画中に出力の設定を変えられないこと
void test_decide_change()
{
	OutputActivity idle;
	CHECK(decide_change(output_change("SimpleOutput/VBitrate"), idle, OutputMode::Simple, "6000", "4500") ==
	      ChangeDecision::Allowed);
	CHECK(decide_change(output_change("SimpleOutput/StreamEncoder"), idle, OutputMode::Simple, "x264", "nvenc") ==
	      ChangeDecision::GuideOnly);

	bool OutputActivity::*const flags[] = {&OutputActivity::streaming, &OutputActivity::recording,
					       &OutputActivity::replay_buffer, &OutputActivity::virtual_camera,
					       &OutputActivity::video_active};
	for (const auto flag : flags) {
		OutputActivity active;
		active.*flag = true;
		CHECK(decide_change(output_change("SimpleOutput/VBitrate"), active, OutputMode::Simple, "6000",
				    "4500") == ChangeDecision::BlockedWhileActive);
		Change video;
		video.target = ChangeTarget::VideoSetting;
		video.key = "Video/FPSCommon";
		CHECK(decide_change(video, active, OutputMode::Simple, "60", "30") ==
		      ChangeDecision::BlockedWhileActive);
		// 出力以外の設定は配信中も提案できる
		Change volume;
		volume.target = ChangeTarget::AudioVolumeDb;
		volume.source = "Mic";
		CHECK(decide_change(volume, active, OutputMode::Simple, "", "") == ChangeDecision::Allowed);
	}

	Change wrong_video;
	wrong_video.target = ChangeTarget::VideoSetting;
	wrong_video.key = "Video/ColorFormat";
	CHECK(decide_change(wrong_video, idle, OutputMode::Simple, "NV12", "P010") == ChangeDecision::GuideOnly);
}

// 配信中の注意を出す対象。音声の変更と音声のフィルターの追加だけ
void test_changes_audio_immediately()
{
	CHECK(changes_audio_immediately(ChangeTarget::AudioMute));
	CHECK(changes_audio_immediately(ChangeTarget::AudioVolumeDb));
	CHECK(changes_audio_immediately(ChangeTarget::AudioMonitoring));
	CHECK(changes_audio_immediately(ChangeTarget::AudioSyncOffsetMs));
	CHECK(changes_audio_immediately(ChangeTarget::AudioTracks));
	CHECK(!changes_audio_immediately(ChangeTarget::SourceVisibility));
	CHECK(!changes_audio_immediately(ChangeTarget::OutputSetting));
	CHECK(changes_audio_immediately(AddKind::NoiseSuppression));
	CHECK(changes_audio_immediately(AddKind::Compressor));
	CHECK(!changes_audio_immediately(AddKind::Crop));
	CHECK(!changes_audio_immediately(AddKind::BrowserSource));
}

Proposal proposal_for(const char *label)
{
	Proposal p;
	p.tool = kToolChangeSettings;
	p.items.push_back({label, "0", "1"});
	return p;
}

void test_proposal_book()
{
	ProposalBook book;
	Proposal change = proposal_for("Micの音量");
	change.items[0].before = "-10.0 dB";
	change.items[0].after = "-6.0 dB";
	change.items[0].record = {{"target", "audio_volume_db"},
				  {"source", "Mic"},
				  {"before_value_json", "-10.0"},
				  {"after_value_json", "-6.0"}};
	const int a = book.add(change);
	CHECK(book.find(a)->state == ProposalState::Pending);
	CHECK(book.mark_applied(a));
	CHECK(!book.mark_applied(a));
	CHECK(!book.mark_rejected(a));

	// 適用の結果は実行の記録として、表示用の値と、元に戻す提案に使える生の値の両方を持つ
	const json applied = json::parse(book.tool_output(a));
	CHECK(applied["status"] == "applied");
	CHECK(applied["items"].size() == 1);
	const json &item = applied["items"][0];
	CHECK(item["operation"] == "change");
	CHECK(item["label"] == "Micの音量");
	CHECK(item["before"] == "-10.0 dB");
	CHECK(item["after"] == "-6.0 dB");
	CHECK(item["before_value_json"] == "-10.0");
	CHECK(item["after_value_json"] == "-6.0");
	CHECK(item["target"] == "audio_volume_db");
	CHECK(item["applies"] == "immediately");

	const int c = book.add(proposal_for("Desktop Audioのミュート"));
	CHECK(book.mark_rejected(c));
	CHECK(json::parse(book.tool_output(c))["status"] == "rejected_by_user");

	const int d = book.add(proposal_for("x"));
	CHECK(book.mark_failed(d, "source not found"));
	const json failed = json::parse(book.tool_output(d));
	CHECK(failed["status"] == "failed");
	CHECK(failed["error"] == "source not found");
	CHECK(json::parse(book.tool_output(999))["status"] == "unknown_proposal");

	// 追加は remove_item の引数の形で記録し、削除は名前だけを返す
	Proposal add;
	add.tool = kToolAddItem;
	add.items.push_back({"Micにフィルタ「Noise」(ノイズ抑制)を追加", "", "既定の設定", ProposalItemKind::Add});
	add.items[0].record = {{"kind", "filter"}, {"source", "Mic"}, {"scene", nullptr}, {"name", "Noise"}};
	const json added = json::parse(book.tool_output(book.add(add)));
	CHECK(added["items"][0]["operation"] == "add");
	CHECK(added["items"][0]["kind"] == "filter");
	CHECK(added["items"][0]["name"] == "Noise");
	CHECK(!added["items"][0].contains("applies"));
	Proposal remove;
	remove.tool = kToolRemoveItem;
	remove.items.push_back({"Micのフィルタ「Noise」(ノイズ抑制)", "", "", ProposalItemKind::Remove});
	CHECK(json::parse(book.tool_output(book.add(remove)))["items"][0]["operation"] == "remove");

	// 出力の設定は反映の時期を結果に含め、承認の画面には時期ごとに一文を出す
	Proposal output = proposal_for("出力の設定「配信 > 映像ビットレート」");
	output.items[0].timing = ApplyTiming::NextStartAllStopped;
	output.items.push_back(
		{"映像の設定「FPS 共通値」", "60", "30", ProposalItemKind::Change, ApplyTiming::VideoReset});
	output.items.push_back({"出力の設定「配信 > 音声ビットレート」", "160", "128", ProposalItemKind::Change,
				ApplyTiming::NextStartAllStopped});
	const json timed = json::parse(book.tool_output(book.add(output)));
	CHECK(timed["items"][0]["applies"] == "next_stream_or_recording_start_after_all_outputs_stopped");
	CHECK(timed["items"][1]["applies"] == "immediately");
	const auto timings = notable_timings(output);
	CHECK(timings.size() == 2);
	CHECK(timings[0] == ApplyTiming::NextStartAllStopped);
	CHECK(timings[1] == ApplyTiming::VideoReset);
	CHECK(notable_timings(change).empty());
}

ParsedResponse response_with_call(const char *call_id)
{
	ParsedResponse r;
	r.ok = true;
	r.output_items.push_back({{"type", "reasoning"}, {"id", "rs"}});
	r.output_items.push_back({{"type", "function_call"}, {"call_id", call_id}, {"name", "get_obs_status"}});
	r.function_calls.push_back({call_id, "get_obs_status", "{}"});
	return r;
}

ParsedResponse response_with_text(const char *text)
{
	ParsedResponse r;
	r.ok = true;
	r.output_items.push_back({{"type", "message"}, {"role", "assistant"}});
	r.text = text;
	return r;
}

long long tokens_for_percent(int percent)
{
	return kConversationLimitTokens * percent / 100;
}

// 会話の長さのゲージ: 10% 刻みのブロックと、50%・70%・90% の色の段階
void test_length_gauge()
{
	CHECK(length_blocks(0) == 0);
	CHECK(length_blocks(1) == 1);
	CHECK(length_blocks(10) == 1);
	CHECK(length_blocks(11) == 2);
	CHECK(length_blocks(95) == 10);
	CHECK(length_blocks(100) == 10);
	CHECK(length_tier(49) == 0);
	CHECK(length_tier(50) == 1);
	CHECK(length_tier(69) == 1);
	CHECK(length_tier(70) == 2);
	CHECK(length_tier(89) == 2);
	CHECK(length_tier(90) == 3);
}

void test_conversation_length_and_carry_over()
{
	Conversation conv;
	conv.add_user_message("映らない");
	conv.add_response(response_with_call("call_1"));
	CHECK(conv.has_pending_calls());
	conv.add_tool_result("call_1", "get_obs_status", "{\"fps\":60}");
	CHECK(!conv.has_pending_calls());
	conv.add_response(response_with_text("原因はキャプチャーの方法です"));
	CHECK(conv.request_items().size() == 5);

	// 要点を引き継ぐボタンは約 50% から
	conv.set_last_input_tokens(tokens_for_percent(49));
	CHECK(conv.length_percent() == 49);
	CHECK(!conv.can_carry_over());
	conv.set_last_input_tokens(tokens_for_percent(50));
	CHECK(conv.can_carry_over());
	CHECK(!conv.at_length_limit());

	// 段階の知らせは一度だけ
	conv.set_last_input_tokens(tokens_for_percent(75));
	CHECK(conv.take_length_notice(kLengthNoticeLongPercent));
	CHECK(!conv.take_length_notice(kLengthNoticeLongPercent));
	CHECK(!conv.take_length_notice(kLengthNoticeNearPercent));

	// 上限の手前で送信を止める
	conv.set_last_input_tokens(tokens_for_percent(kLengthBlockPercent) - 1000);
	CHECK(!conv.at_length_limit());
	conv.set_last_input_tokens(tokens_for_percent(kLengthBlockPercent));
	CHECK(conv.at_length_limit());

	const json summary_request = conv.build_summary_request(ModelConfig{}, "inst", build_tools_json());
	CHECK(summary_request["tool_choice"] == "none");
	CHECK(summary_request["input"].size() == 6);

	// 要点を引き継ぐと新しい会話になる。画面は引き継いだ要点だけになり、長さと知らせも初めからになる
	conv.start_with_summary("要点: 画面が映らない相談");
	CHECK(conv.display().size() == 1);
	CHECK(conv.display()[0].kind == DisplayEntry::Kind::Summary);
	CHECK(conv.length_percent() == 0);
	CHECK(!conv.at_length_limit());
	auto items = conv.request_items();
	CHECK(items.size() == 1);
	CHECK(contains_word(items[0]["content"].get<std::string>(), "要点: 画面が映らない相談"));
	conv.add_user_message("まだ映らない");
	items = conv.request_items();
	CHECK(items.size() == 2);
	CHECK(!contains_word(json(items).dump(), "call_1"));
	conv.set_last_input_tokens(tokens_for_percent(76));
	CHECK(conv.take_length_notice(kLengthNoticeLongPercent));

	// 「新しい会話」は画面とモデルに渡す内容の両方を消す
	conv.clear();
	CHECK(conv.display().empty());
	CHECK(conv.request_items().empty());
	CHECK(!conv.can_carry_over());
}

// 決めた応答を順に返す偽の送り先。async が真なら、done を呼ばずに保持する
class FakeTransport : public Transport {
public:
	struct Reply {
		long status;
		json body;
		std::string error;
	};
	std::deque<Reply> replies;
	std::vector<json> requests;
	// 検査の要求と、検査の応答。応答が無ければ、すべて問題なしを返す
	std::vector<json> moderation_requests;
	std::deque<Reply> moderation_replies;
	bool async = false;
	Done held;
	int cancels = 0;

	void send(Endpoint endpoint, const json &request, Done done) override
	{
		if (endpoint == Endpoint::Moderations && !async) {
			moderation_requests.push_back(request);
			Reply reply = moderation_replies.empty() ? Reply{200, clean_moderation(request), ""}
								 : moderation_replies.front();
			if (!moderation_replies.empty())
				moderation_replies.pop_front();
			done(reply.status, reply.body, reply.error);
			return;
		}
		if (endpoint == Endpoint::Responses)
			requests.push_back(request);
		if (async) {
			held = std::move(done);
			return;
		}
		Reply reply = replies.empty() ? Reply{200, text_body("(no more replies)"), ""} : replies.front();
		if (!replies.empty())
			replies.pop_front();
		done(reply.status, reply.body, reply.error);
	}
	void cancel() override { ++cancels; }

	static json clean_moderation(const json &request)
	{
		json results = json::array();
		for (size_t i = 0; i < request["input"].size(); ++i)
			results.push_back({{"flagged", false}});
		return {{"results", results}};
	}
	static json moderation_body(std::initializer_list<bool> flags)
	{
		json results = json::array();
		for (const bool flag : flags)
			results.push_back({{"flagged", flag}});
		return {{"results", results}};
	}

	static json text_body(const char *text)
	{
		return {{"status", "completed"},
			{"output",
			 json::array({{{"type", "message"},
				       {"role", "assistant"},
				       {"content", json::array({{{"type", "output_text"}, {"text", text}}})}}})},
			{"usage", {{"input_tokens", 1000}}}};
	}
	static json call_body(const char *call_id, const char *name, const std::string &arguments,
			      long long input_tokens = 1000)
	{
		return {{"status", "completed"},
			{"output", json::array({{{"type", "function_call"},
						 {"call_id", call_id},
						 {"name", name},
						 {"arguments", arguments}}})},
			{"usage", {{"input_tokens", input_tokens}}}};
	}
};

class FakeHost : public ToolHost {
public:
	std::vector<std::string> read_calls;
	int applies = 0;
	std::vector<RemoveRequest> removals;
	bool prepare_ok = true;
	bool apply_ok = true;

	std::string run_read_tool(const std::string &name, const std::string &) override
	{
		read_calls.push_back(name);
		return "{\"fps\":60}";
	}
	Parsed<Proposal> prepare_change(const ChangeRequest &request) override
	{
		Parsed<Proposal> result;
		if (!prepare_ok) {
			result.error = "guide only";
			return result;
		}
		for (const auto &change : request.changes)
			result.value.items.push_back({change.source, "0", change.value.dump()});
		result.ok = true;
		return result;
	}
	Parsed<Proposal> prepare_add(const AddRequest &request) override
	{
		Parsed<Proposal> result;
		result.value.items.push_back({request.name, "", request.kind->name, ProposalItemKind::Add});
		result.ok = true;
		return result;
	}
	Parsed<Proposal> prepare_remove(const RemoveRequest &request) override
	{
		removals.push_back(request);
		Parsed<Proposal> result;
		// プラグインの側では目印の無いものを拒む。ここでは "User" で始まる名前を利用者が作ったものとする
		if (request.name.rfind("User", 0) == 0) {
			result.error = "This item was not added by the assistant, so it cannot be deleted.";
			return result;
		}
		result.value.items.push_back({request.name, "", "", ProposalItemKind::Remove});
		result.ok = true;
		return result;
	}
	bool apply(const Proposal &, std::string &error) override
	{
		++applies;
		if (!apply_ok)
			error = "source not found";
		return apply_ok;
	}
};

const json &last_input_item(const json &request)
{
	return request["input"].back();
}

const std::string kVolumeArgs = one_change("audio_volume_db", "Mic", "-6");

void test_session_read_tool_loop()
{
	FakeTransport transport;
	FakeHost host;
	Session session(transport, host);
	transport.replies.push_back({200, FakeTransport::call_body("c1", kToolGetObsStatus, "{}"), ""});
	transport.replies.push_back({200, FakeTransport::text_body("原因はエンコードの負荷です"), ""});

	session.send_user_message("カクつく");
	CHECK(session.state() == SessionState::Idle);
	CHECK(host.read_calls.size() == 1);
	CHECK(transport.requests.size() == 2);
	// 2 回目の要求は、道具の結果を最後に持つ
	CHECK(last_input_item(transport.requests[1])["type"] == "function_call_output");
	CHECK(last_input_item(transport.requests[1])["call_id"] == "c1");
	CHECK(session.conversation().display().back().text == "原因はエンコードの負荷です");
}

void test_session_approval()
{
	FakeTransport transport;
	FakeHost host;
	Session session(transport, host);
	transport.replies.push_back({200, FakeTransport::call_body("c1", kToolChangeSettings, kVolumeArgs), ""});
	transport.replies.push_back({200, FakeTransport::text_body("下げました"), ""});

	session.send_user_message("声が大きい");
	// 承認を待つ間は要求を送らず、OBS を変えない
	CHECK(session.state() == SessionState::AwaitingApproval);
	CHECK(transport.requests.size() == 1);
	CHECK(host.applies == 0);
	CHECK(session.proposals().all().size() == 1);
	// 承認を待つ間は新しい発話を受け付けない
	session.send_user_message("別の話");
	CHECK(transport.requests.size() == 1);

	session.approve(1);
	CHECK(host.applies == 1);
	CHECK(session.proposals().find(1)->state == ProposalState::Applied);
	CHECK(transport.requests.size() == 2);
	const json output = last_input_item(transport.requests[1]);
	CHECK(output["call_id"] == "c1");
	const json result = json::parse(output["output"].get<std::string>());
	CHECK(result["status"] == "applied");
	// 適用の結果に、項目と変える前と後の値が入る
	CHECK(result["items"].size() == 1);
	CHECK(result["items"][0]["label"] == "Mic");
	CHECK(result["items"][0]["before"] == "0");
	CHECK(result["items"][0]["after"] == "-6");
	CHECK(session.state() == SessionState::Idle);
}

// 削除の道具も、承認を経て適用する。利用者が作ったものは提案にならず、誤りをモデルに返す
void test_session_remove()
{
	{
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{200,
			 FakeTransport::call_body(
				 "c1", kToolRemoveItem,
				 R"({"reason":"r","kind":"filter","source":"Mic","scene":null,"name":"Noise"})"),
			 ""});
		transport.replies.push_back({200, FakeTransport::text_body("削除しました"), ""});
		session.send_user_message("さっきのフィルタを消して");
		CHECK(session.state() == SessionState::AwaitingApproval);
		CHECK(host.removals.size() == 1);
		CHECK(host.removals[0].is_filter);
		CHECK(host.applies == 0);
		CHECK(session.proposals().find(1)->tool == kToolRemoveItem);
		session.approve(1);
		CHECK(host.applies == 1);
		CHECK(json::parse(last_input_item(transport.requests[1])["output"].get<std::string>())["status"] ==
		      "applied");
	}
	{
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{200,
			 FakeTransport::call_body(
				 "c1", kToolRemoveItem,
				 R"({"reason":"r","kind":"source","source":null,"scene":"Main","name":"User camera"})"),
			 ""});
		transport.replies.push_back({200, FakeTransport::text_body("削除できません"), ""});
		session.send_user_message("カメラを消して");
		CHECK(session.proposals().all().empty());
		CHECK(host.applies == 0);
		CHECK(contains_word(last_input_item(transport.requests[1])["output"].get<std::string>(),
				    "not added by the assistant"));
		CHECK(session.state() == SessionState::Idle);
	}
	{
		// シーンの削除は引数の検査で拒み、OBS とつなぐ層を呼ばない
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{200,
			 FakeTransport::call_body(
				 "c1", kToolRemoveItem,
				 R"({"reason":"r","kind":"scene","source":null,"scene":"Main","name":"Main"})"),
			 ""});
		transport.replies.push_back({200, FakeTransport::text_body("できません"), ""});
		session.send_user_message("シーンを消して");
		CHECK(host.removals.empty());
		CHECK(session.proposals().all().empty());
	}
}

void test_session_reject_and_failures()
{
	{
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{200, FakeTransport::call_body("c1", kToolChangeSettings, kVolumeArgs), ""});
		transport.replies.push_back({200, FakeTransport::text_body("わかりました"), ""});
		session.send_user_message("x");
		session.reject(1);
		CHECK(host.applies == 0);
		const json output = last_input_item(transport.requests[1]);
		CHECK(json::parse(output["output"].get<std::string>())["status"] == "rejected_by_user");
	}
	{
		// 引数の誤りは提案にせず、誤りをモデルに返して続ける
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{200,
			 FakeTransport::call_body("c1", kToolChangeSettings, one_change("delete_source", "Mic", "1")),
			 ""});
		transport.replies.push_back({200, FakeTransport::text_body("できません"), ""});
		session.send_user_message("消して");
		CHECK(session.proposals().all().empty());
		CHECK(transport.requests.size() == 2);
		CHECK(json::parse(last_input_item(transport.requests[1])["output"].get<std::string>())["status"] ==
		      "error");
		CHECK(session.state() == SessionState::Idle);
	}
	{
		// OBS の側で組み立てられない提案 (案内に回す項目など) も、誤りをモデルに返す
		FakeTransport transport;
		FakeHost host;
		host.prepare_ok = false;
		Session session(transport, host);
		transport.replies.push_back(
			{200, FakeTransport::call_body("c1", kToolChangeSettings, kVolumeArgs), ""});
		transport.replies.push_back({200, FakeTransport::text_body("手順を案内します"), ""});
		session.send_user_message("x");
		CHECK(session.proposals().all().empty());
		CHECK(contains_word(last_input_item(transport.requests[1])["output"].get<std::string>(), "guide only"));
	}
	{
		// 適用に失敗したら failed をモデルに返し、画面に知らせる
		FakeTransport transport;
		FakeHost host;
		host.apply_ok = false;
		Session session(transport, host);
		transport.replies.push_back(
			{200, FakeTransport::call_body("c1", kToolChangeSettings, kVolumeArgs), ""});
		transport.replies.push_back({200, FakeTransport::text_body("失敗しました"), ""});
		session.send_user_message("x");
		session.approve(1);
		CHECK(session.proposals().find(1)->state == ProposalState::Failed);
		CHECK(json::parse(last_input_item(transport.requests[1])["output"].get<std::string>())["status"] ==
		      "failed");
		bool noticed = false;
		for (const auto &entry : session.conversation().display())
			noticed = noticed || entry.text == kNoticeApplyFailed;
		CHECK(noticed);
	}
}

void test_session_limits_and_errors()
{
	{
		// 道具の呼び出しが止まらなくても、1 回の発話あたりの要求は上限で止める
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		for (int i = 0; i < 20; ++i)
			transport.replies.push_back(
				{200, FakeTransport::call_body(("c" + std::to_string(i)).c_str(), kToolReadLog, "{}"),
				 ""});
		session.send_user_message("x");
		CHECK(transport.requests.size() == static_cast<size_t>(kMaxStepsPerTurn));
		CHECK(session.state() == SessionState::Idle);
		CHECK(session.conversation().display().back().text == kNoticeTooManySteps);
	}
	{
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back(
			{401, json::parse(R"({"error":{"type":"invalid_request_error","code":"invalid_api_key"}})"),
			 ""});
		session.send_user_message("x");
		CHECK(session.conversation().display().back().text == kNoticeInvalidKey);
		CHECK(session.state() == SessionState::Idle);
		transport.replies.push_back({0, json(), "Could not resolve host"});
		session.send_user_message("y");
		CHECK(session.conversation().display().back().text == kNoticeNetworkError);
		CHECK(session.conversation().display().back().detail == "Could not resolve host");
	}
}

bool has_notice(const Session &session, const char *key)
{
	for (const auto &entry : session.conversation().display()) {
		if (entry.kind == DisplayEntry::Kind::Notice && entry.text == key)
			return true;
	}
	return false;
}

void test_session_length_and_cancel()
{
	FakeTransport transport;
	FakeHost host;
	Session session(transport, host);
	transport.replies.push_back(
		{200, FakeTransport::call_body("c1", kToolGetObsStatus, "{}", tokens_for_percent(60)), ""});
	json long_body = FakeTransport::text_body("長い相談の答え");
	long_body["usage"]["input_tokens"] = tokens_for_percent(91);
	transport.replies.push_back({200, long_body, ""});
	session.send_user_message("最初の相談");
	// 75% を飛ばして 90% を越えたときは、90% の知らせだけを出す
	CHECK(has_notice(session, kNoticeLengthNear));
	CHECK(!has_notice(session, kNoticeLengthLong));
	CHECK(session.conversation().can_carry_over());

	// 要点を引き継ぐと、実行の記録も消えた新しい会話になる
	transport.replies.push_back({200, FakeTransport::text_body("要点の本文"), ""});
	session.carry_over();
	CHECK(session.state() == SessionState::Idle);
	CHECK(transport.requests.back()["tool_choice"] == "none");
	CHECK(session.conversation().length_percent() == 0);
	CHECK(session.proposals().all().empty());
	CHECK(has_notice(session, kNoticeCarriedOver));

	transport.replies.push_back({200, FakeTransport::text_body("続きの答え"), ""});
	session.send_user_message("続き");
	const json &input = transport.requests.back()["input"];
	CHECK(input.size() == 2);
	CHECK(contains_word(input[0]["content"].get<std::string>(), "要点の本文"));

	{
		// 上限の手前では送らない。道具の往復の途中で達したときも止める
		FakeTransport t;
		FakeHost h;
		Session s(t, h);
		t.replies.push_back(
			{200, FakeTransport::call_body("c1", kToolGetObsStatus, "{}", tokens_for_percent(96)), ""});
		s.send_user_message("相談");
		CHECK(t.requests.size() == 1);
		CHECK(has_notice(s, kNoticeLengthLimit));
		CHECK(s.state() == SessionState::Idle);
		s.send_user_message("もう一度");
		CHECK(t.requests.size() == 1);
	}

	// 取り消した後に届いた古い応答は捨てる
	transport.async = true;
	session.send_user_message("もう一つ");
	CHECK(session.state() == SessionState::Waiting);
	session.cancel();
	CHECK(transport.cancels == 1);
	CHECK(session.state() == SessionState::Idle);
	const size_t shown = session.conversation().display().size();
	transport.held(200, FakeTransport::text_body("遅れて届いた答え"), "");
	CHECK(session.conversation().display().size() == shown);

	session.clear();
	CHECK(session.conversation().display().empty());
	CHECK(session.proposals().all().empty());
}

// 送る前の検査で問題ありとされた内容は、Responses API に送らない
void test_session_moderation()
{
	CHECK(parse_moderation(200, FakeTransport::moderation_body({false, true}), 2).ok);
	CHECK(!parse_moderation(200, FakeTransport::moderation_body({false}), 2).ok);
	CHECK(!parse_moderation(500, FakeTransport::moderation_body({false}), 1).ok);
	CHECK(build_moderation_request({"a", "b"})["input"].size() == 2);

	{
		// 道具の結果が問題ありとされたら、送らない旨の文に差し替えて送る。画面の「送った内容」も差し替える
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.replies.push_back({200, FakeTransport::call_body("c1", kToolGetObsStatus, "{}"), ""});
		transport.replies.push_back({200, FakeTransport::text_body("答え"), ""});
		transport.moderation_replies.push_back({200, FakeTransport::moderation_body({false}), ""});
		transport.moderation_replies.push_back({200, FakeTransport::moderation_body({true}), ""});
		session.send_user_message("映らない");
		CHECK(transport.moderation_requests.size() == 2);
		// 2 回目の検査は、まだ検査していない道具の結果だけ
		CHECK(transport.moderation_requests[1]["input"].size() == 1);
		CHECK(transport.requests.size() == 2);
		const json sent = last_input_item(transport.requests[1]);
		CHECK(sent["output"] == kWithheldToolOutput);
		CHECK(!contains_word(transport.requests[1].dump(), "\\\"fps\\\":60"));
		bool noticed = false;
		bool display_replaced = false;
		for (const auto &entry : session.conversation().display()) {
			noticed = noticed || entry.text == kNoticeWithheld;
			display_replaced = display_replaced || (entry.kind == DisplayEntry::Kind::ToolResult &&
								entry.text == kWithheldToolOutput);
		}
		CHECK(noticed);
		CHECK(display_replaced);
	}
	{
		// 利用者の発言が問題ありとされたら、送らない旨の文に差し替える。画面には利用者の発言を残す
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.moderation_replies.push_back({200, FakeTransport::moderation_body({true}), ""});
		transport.replies.push_back({200, FakeTransport::text_body("お答えできません"), ""});
		session.send_user_message("問題のある発言");
		CHECK(transport.requests.size() == 1);
		CHECK(transport.requests[0]["input"][0]["content"] == kWithheldUserText);
		CHECK(!contains_word(transport.requests[0].dump(), "問題のある発言"));
		CHECK(session.conversation().display()[0].text == "問題のある発言");
	}
	{
		// 検査に失敗したら何も送らずに止め、次の送信で検査をやり直す
		FakeTransport transport;
		FakeHost host;
		Session session(transport, host);
		transport.moderation_replies.push_back({500, json::object(), ""});
		session.send_user_message("最初");
		CHECK(transport.requests.empty());
		CHECK(session.state() == SessionState::Idle);
		CHECK(session.conversation().display().back().text == kNoticeModerationFailed);
		transport.replies.push_back({200, FakeTransport::text_body("答え"), ""});
		session.send_user_message("二回目");
		CHECK(transport.moderation_requests.back()["input"].size() == 2);
		CHECK(transport.requests.size() == 1);
	}
}

// OBS 33 のモニタリングは有効と無効だけで、monitor_only には対応する値が無い
void test_monitoring_values()
{
	bool enabled = true;
	CHECK(monitoring_enabled_from_name(kMonitoringOff, enabled) && !enabled);
	CHECK(monitoring_enabled_from_name(kMonitoringAndOutput, enabled) && enabled);
	CHECK(!monitoring_enabled_from_name("monitor_only", enabled));
	CHECK(std::string(monitoring_name(true)) == kMonitoringAndOutput);
}

// トラック 1 が最下位のビット、トラック 6 が 6 番目のビット
void test_track_mixers()
{
	CHECK(tracks_to_mixers(json::array({1})) == 0x1u);
	CHECK(tracks_to_mixers(json::array({1, 6})) == 0x21u);
	CHECK(tracks_to_mixers(json::array()) == 0u);
	// OBS は 6 本より上のビットも持ちうるが、道具では 1-6 だけを見せる
	CHECK(mixers_to_tracks(0xFFu) == json::array({1, 2, 3, 4, 5, 6}));
	CHECK(mixers_to_tracks(0x21u) == json::array({1, 6}));
}

void test_output_key_and_config_text()
{
	OutputKey key;
	CHECK(parse_output_key("SimpleOutput/VBitrate", key));
	CHECK(key.kind == OutputKey::Kind::Config && key.container == "SimpleOutput" && key.name == "VBitrate");
	CHECK(parse_output_key("streamEncoder.json/bitrate", key));
	CHECK(key.kind == OutputKey::Kind::StreamEncoderJson && key.name == "bitrate");
	CHECK(parse_output_key("recordEncoder.json/rate_control", key));
	CHECK(key.kind == OutputKey::Kind::RecordEncoderJson);
	CHECK(!parse_output_key("VBitrate", key));
	CHECK(!parse_output_key("SimpleOutput/", key));
	CHECK(!parse_output_key("streamEncoder.json/a/b", key));

	std::string text;
	CHECK(to_config_text(json(4500), text) && text == "4500");
	CHECK(to_config_text(json(true), text) && text == "true");
	CHECK(to_config_text(json("hybrid_mp4"), text) && text == "hybrid_mp4");
	CHECK(to_config_text(json(2.5), text) && text == "2.5");
	CHECK(!to_config_text(json::array({1}), text));
	CHECK(!to_config_text(json(nullptr), text));
}

// 反映の成否の判定に使う値。OBS の丸めと FPS の表に合わないと、成功した変更を失敗として戻してしまう
void test_expected_video()
{
	VideoConfig config;
	config.base_cx = 1920;
	config.base_cy = 1080;
	config.output_cx = 1282;
	config.output_cy = 721;
	config.fps_type = 0;
	config.fps_common = "59.94";
	VideoExpectation v = expected_video(config);
	CHECK(v.base_width == 1920 && v.base_height == 1080);
	CHECK(v.output_width == 1280 && v.output_height == 720);
	CHECK(v.fps_num == 60000 && v.fps_den == 1001);

	config.fps_common = "unknown";
	CHECK(expected_video(config).fps_num == 30);

	config.fps_type = 2;
	config.fps_num = 120;
	config.fps_den = 2;
	v = expected_video(config);
	CHECK(v.fps_num == 120 && v.fps_den == 2);

	// 32 未満は OBS が既定の解像度に直す
	config.base_cx = 0;
	config.output_cx = 10;
	v = expected_video(config);
	CHECK(v.base_width == 1920 && v.output_width == 1920 && v.output_height == 1080);
}

void test_log_file_selection()
{
	CHECK(pick_latest_log_file({"2026-11-10 08-00-00.txt", "2026-11-12 07-59-59.txt", "2026-11-11 23-00-00.txt"}) ==
	      "2026-11-12 07-59-59.txt");
	// クラッシュの記録や他のファイルは対象にしない
	CHECK(pick_latest_log_file({"2026-11-10 08-00-00.txt", "Crash 2026-11-12 09-00-00.txt", "zz.txt",
				    "2026-11-13 08-00-00.log"}) == "2026-11-10 08-00-00.txt");
	CHECK(pick_latest_log_file({}).empty());

	CHECK(last_lines("a\nb\nc\n", 2) == "b\nc");
	CHECK(last_lines("a\r\nb\r\nc", 2) == "b\nc");
	CHECK(last_lines("a\nb", 300) == "a\nb");
	CHECK(last_lines("", 300).empty());

	// 依頼者の PC のログで末尾 300 行のうち 117 行を占めた、ブラウザソースの同じエラーの繰り返し。
	// 時刻と引用符の中の名前が違っても 1 行にまとめ、最後に現れた位置に回数と最初の時刻を添える
	const std::string repeated = "10:00:00.000: [obs-browser: 'A'] WS Closed\r\n"
				     "10:00:01.000: Output 'simple_stream': stopping\r\n"
				     "10:00:02.500: [obs-browser: 'B'] WS Closed\r\n"
				     "\n"
				     "10:00:03.000: \n"
				     "10:00:04.000: [obs-browser: 'A'] WS Closed\n"
				     "\n";
	CHECK(collapse_repeated_lines(repeated) ==
	      "10:00:01.000: Output 'simple_stream': stopping\n"
	      "\n"
	      "10:00:03.000: \n"
	      "10:00:04.000: [obs-browser: 'A'] WS Closed [same message 3 times, first at 10:00:00.000]\n"
	      "");
	// 繰り返しの無いログと時刻の無い行は、そのまま返す
	CHECK(collapse_repeated_lines("a\nb 'x\nc") == "a\nb 'x\nc");
	CHECK(collapse_repeated_lines("x\nx") == "x [same message 2 times]");
	CHECK(collapse_repeated_lines("").empty());
}

// 承認の画面の文言を引くキー。OBS 本体の訳とプラグインの訳を、実行時と同じく別々に引く
struct FakeUiText {
	std::map<std::string, std::string> obs_texts = {
		{"Basic.Settings.Output.Adv.Streaming", "配信"},
		{"Basic.Settings.Output.Adv.Recording", "録画"},
		{"Basic.Settings.Audio", "音声"},
		{"Basic.Settings.Output.VideoBitrate", "映像ビットレート"},
		{"Basic.Settings.Output.AudioBitrate", "音声ビットレート"},
		{"Basic.Settings.Output.Adv.Audio.Track1", "トラック 1"},
		{"Basic.Settings.Output.Adv.Audio.Track2", "トラック 2"},
		{"Basic.Settings.Output.Adv.Encoder", "エンコーダ設定"},
		{"Basic.Settings.Video.BaseResolution", "基本 (キャンバス) 解像度"},
		{"Width", "幅"},
		{"Yes", "はい"},
		{"No", "いいえ"},
		{"Mute", "ミュート"},
		{"Unmute", "ミュート解除"},
		{"Basic.AdvAudio.Monitoring.None", "モニターオフ"},
		{"Basic.AdvAudio.Monitoring.Both", "モニタリング有効"},
		{"Basic.Settings.Output.Format.hMP4", "Hybrid MP4 (.mp4)"},
		{"Basic.Settings.Video.DownscaleFilter.Lanczos", "ランチョス (先鋭化スケーリング、36サンプル)"},
		{"Basic.Settings.Video.FPSFraction", "FPS 分数値"},
	};
	std::map<std::string, std::string> plugin_texts = {
		{"Value.ListSeparator", "、"},
		{"Value.NoTracks", "なし"},
	};
	std::set<std::string> obs_keys;
	std::set<std::string> plugin_keys;

	UiText ui()
	{
		return {[this](const char *key) { return lookup(obs_texts, obs_keys, key); },
			[this](const char *key) {
				return lookup(plugin_texts, plugin_keys, key);
			}};
	}

	// 表に無いキーは [キー] で返し、引いたキーを記録する
	static std::string lookup(const std::map<std::string, std::string> &texts, std::set<std::string> &used,
				  const char *key)
	{
		used.insert(key);
		const auto it = texts.find(key);
		return it != texts.end() ? it->second : "[" + std::string(key) + "]";
	}
};

// data/locale の ini のキー
std::set<std::string> locale_keys(const char *file)
{
	std::set<std::string> keys;
	std::ifstream in(std::string(AIA_SOURCE_DIR) + "/data/locale/" + file);
	CHECK(in.good());
	std::string line;
	while (std::getline(in, line)) {
		const size_t eq = line.find('=');
		if (eq != std::string::npos)
			keys.insert(line.substr(0, eq));
	}
	return keys;
}

// 日英の ini が同じキーを持ち、どちらにも重複が無いこと
void test_locale_files()
{
	std::map<std::string, std::vector<std::string>> lists;
	for (const char *file : {"ja-JP.ini", "en-US.ini"}) {
		std::ifstream in(std::string(AIA_SOURCE_DIR) + "/data/locale/" + file);
		CHECK(in.good());
		std::string line;
		while (std::getline(in, line)) {
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				lists[file].push_back(line.substr(0, eq));
		}
		const std::set<std::string> unique(lists[file].begin(), lists[file].end());
		if (unique.size() != lists[file].size())
			std::printf("duplicate keys in %s\n", file);
		CHECK(unique.size() == lists[file].size());
	}
	CHECK(locale_keys("ja-JP.ini") == locale_keys("en-US.ini"));
}

void test_display_names()
{
	FakeUiText fake;
	const UiText ui = fake.ui();

	CHECK(output_setting_label("SimpleOutput/VBitrate", ui) == "配信 > 映像ビットレート");
	CHECK(output_setting_value("SimpleOutput/VBitrate", json("4500"), ui) == "4500 kbps");
	CHECK(output_setting_label("AdvOut/Track2Bitrate", ui) == "音声 > トラック 2 > 音声ビットレート");
	CHECK(output_setting_label("Video/BaseCX", ui) == "基本 (キャンバス) 解像度 > 幅");
	CHECK(output_setting_value("Video/FPSType", json("2"), ui) == "FPS 分数値");
	CHECK(output_setting_value("SimpleOutput/UseAdvanced", json("true"), ui) == "はい");
	CHECK(output_setting_value("AdvOut/VodTrackEnabled", json("false"), ui) == "いいえ");
	CHECK(output_setting_value("SimpleOutput/RecTracks", json("3"), ui) == "トラック 1、トラック 2");
	CHECK(output_setting_value("AdvOut/RecTracks", json("0"), ui) == "なし");
	CHECK(output_setting_value("AdvOut/FLVTrack", json("2"), ui) == "トラック 2");
	CHECK(output_setting_value("AdvOut/RecFormat2", json("hybrid_mp4"), ui) == "Hybrid MP4 (.mp4)");
	CHECK(output_setting_value("AdvOut/RescaleFilter", json("4"), ui) ==
	      "ランチョス (先鋭化スケーリング、36サンプル)");
	// 選択肢に無い値と未設定は、そのまま示す
	CHECK(output_setting_value("AdvOut/RecFormat2", json("avi"), ui) == "avi");
	CHECK(output_setting_value("AdvOut/RescaleFilter", json("1"), ui) == "1");
	CHECK(output_setting_value("SimpleOutput/VBitrate", json(), ui) == "-");
	CHECK(output_setting_value("SimpleOutput/x264Settings", json(""), ui) == "-");
	CHECK(output_setting_label("SimpleOutput/StreamEncoder", ui).empty());
	CHECK(encoder_setting_label(false, "ビットレート", ui) == "配信 > エンコーダ設定 > ビットレート");

	CHECK(audio_value_text(ChangeTarget::AudioMute, json(true), ui) == "ミュート");
	CHECK(audio_value_text(ChangeTarget::AudioMute, json(false), ui) == "ミュート解除");
	CHECK(audio_value_text(ChangeTarget::AudioVolumeDb, json(-6), ui) == "-6.0 dB");
	CHECK(audio_value_text(ChangeTarget::AudioVolumeDb, json(-12.34), ui) == "-12.3 dB");
	CHECK(audio_value_text(ChangeTarget::AudioMonitoring, json(kMonitoringOff), ui) == "モニターオフ");
	CHECK(audio_value_text(ChangeTarget::AudioMonitoring, json(kMonitoringAndOutput), ui) == "モニタリング有効");
	CHECK(audio_value_text(ChangeTarget::AudioSyncOffsetMs, json(150), ui) == "150 ms");
	CHECK(audio_value_text(ChangeTarget::AudioTracks, json::array({1, 2}), ui) == "トラック 1、トラック 2");
	CHECK(audio_value_text(ChangeTarget::AudioTracks, json::array(), ui) == "なし");

	CHECK(plain_value_text(json(), ui) == "-");
	CHECK(plain_value_text(json(true), ui) == "はい");
	CHECK(plain_value_text(json(2), ui) == "2");
	CHECK(plain_value_text(json::array({"a", 1}), ui) == "a、1");
	CHECK(number_text(json(30), " dB") == "30 dB");

	// 書き換えてよい config の項目は、すべて設定画面の名前で示せる
	for (const auto &key : writable_output_config_keys())
		CHECK(!output_setting_label(key, ui).empty());

	// 引いた訳のキーは、どれもプラグインの訳に置いてある。OBS 本体の訳は "OBS.<キー>" に写してある
	for (const auto &key :
	     {"Value.Enabled", "Value.Disabled", "Proposal.DefaultSettings", "Proposal.AudioProperty"})
		fake.plugin_keys.insert(key);
	for (const char *key : {"Show", "Hide"})
		fake.obs_keys.insert(key);
	for (ChangeTarget target : {ChangeTarget::AudioMute, ChangeTarget::AudioVolumeDb, ChangeTarget::AudioMonitoring,
				    ChangeTarget::AudioSyncOffsetMs, ChangeTarget::AudioTracks})
		fake.obs_keys.insert(audio_property_key(target));
	for (int value = 0; value <= 5; ++value)
		output_setting_value("AdvOut/RescaleFilter", json(std::to_string(value)), ui);
	for (const char *format : {"flv", "mkv", "mp4", "mov", "hybrid_mp4", "hybrid_mov", "fragmented_mp4",
				   "fragmented_mov", "mpegts", "hls"})
		output_setting_value("AdvOut/RecFormat2", json(format), ui);
	for (int value = 0; value <= 2; ++value)
		output_setting_value("Video/FPSType", json(std::to_string(value)), ui);
	output_setting_value("AdvOut/RecTracks", json("63"), ui);
	encoder_setting_label(true, "", ui);
	// 基本の出力モードの映像エンコーダの選択肢も、OBS の訳を引けない版のために写しを持つ
	for (const auto &choice : simple_encoder_choices())
		fake.obs_keys.insert(choice.locale_key);
	// 承認の画面の反映の時期と削除の文言
	for (const ApplyTiming timing :
	     {ApplyTiming::VideoReset, ApplyTiming::NextStartAllStopped, ApplyTiming::NextStart,
	      ApplyTiming::NextStreamStart, ApplyTiming::NextRecordingStart})
		fake.plugin_keys.insert(apply_timing_notice_key(timing));
	for (const char *key : {"Proposal.Title", "Proposal.RemoveLine", "Proposal.RemoveFilter",
				"Proposal.RemoveSource", "Proposal.RemoveIrreversible", "Tool.remove_item"})
		fake.plugin_keys.insert(key);
	for (const char *file : {"ja-JP.ini", "en-US.ini"}) {
		const std::set<std::string> keys = locale_keys(file);
		for (const auto &key : fake.plugin_keys) {
			if (!keys.count(key))
				std::printf("missing in %s: %s\n", file, key.c_str());
			CHECK(keys.count(key));
		}
		for (const auto &key : fake.obs_keys) {
			if (!keys.count("OBS." + key))
				std::printf("missing in %s: OBS.%s\n", file, key.c_str());
			CHECK(keys.count("OBS." + key));
		}
	}
}

} // namespace

int main()
{
	test_no_forbidden_tools();
	test_tools_json();
	test_add_kinds();
	test_parse_remove_item();
	test_added_mark();
	test_parse_add_item();
	test_parse_change_settings();
	test_output_settings_policy();
	test_output_apply_timing();
	test_build_request();
	test_parse_response();
	test_tool_output_limits();
	test_mask_secrets();
	test_decide_change();
	test_proposal_book();
	test_changes_audio_immediately();
	test_length_gauge();
	test_conversation_length_and_carry_over();
	test_session_read_tool_loop();
	test_session_approval();
	test_session_reject_and_failures();
	test_session_remove();
	test_session_limits_and_errors();
	test_session_length_and_cancel();
	test_session_moderation();
	test_monitoring_values();
	test_track_mixers();
	test_output_key_and_config_text();
	test_expected_video();
	test_log_file_selection();
	test_display_names();
	test_locale_files();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
