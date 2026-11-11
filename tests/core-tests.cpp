// libobs に依存しない層の単体テスト。外部の API は呼ばない

#include "core/add-kinds.h"
#include "core/change-request.h"
#include "core/conversation.h"
#include "core/obs-values.h"
#include "core/output-settings-policy.h"
#include "core/proposals.h"
#include "core/responses.h"
#include "core/secrets.h"
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

Proposal proposal_for(const char *target_id)
{
	Proposal p;
	p.tool = kToolChangeSettings;
	p.items.push_back({"label", json(0), json(1), target_id});
	return p;
}

void test_proposal_book()
{
	ProposalBook book;
	const int a = book.add(proposal_for("audio_volume_db|Mic"));
	const int b = book.add(proposal_for("audio_volume_db|Mic"));
	const int c = book.add(proposal_for("audio_mute|Desktop"));
	CHECK(book.find(a)->state == ProposalState::Pending);

	// 承認前は取り消せない
	CHECK(!book.can_undo(a));
	CHECK(book.mark_applied(a));
	CHECK(!book.mark_applied(a));
	CHECK(!book.mark_rejected(a));
	CHECK(book.can_undo(a));

	// 後から同じ対象を変えた提案が適用されている間は、先の提案を取り消せない
	CHECK(book.mark_applied(b));
	CHECK(!book.can_undo(a));
	CHECK(!book.mark_undone(a));
	CHECK(book.mark_undone(b));
	CHECK(book.find(b)->state == ProposalState::Undone);
	CHECK(book.can_undo(a));
	CHECK(!book.mark_undone(b));

	CHECK(book.mark_rejected(c));
	CHECK(!book.can_undo(c));
	CHECK(json::parse(book.tool_output(c))["status"] == "rejected_by_user");

	const int d = book.add(proposal_for("x"));
	CHECK(book.mark_failed(d, "source not found"));
	const json failed = json::parse(book.tool_output(d));
	CHECK(failed["status"] == "failed");
	CHECK(failed["error"] == "source not found");
	CHECK(json::parse(book.tool_output(999))["status"] == "unknown_proposal");
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

void test_conversation_compaction()
{
	Conversation conv;
	conv.add_user_message("映らない");
	conv.add_response(response_with_call("call_1"));
	CHECK(conv.has_pending_calls());
	// 関数の呼び出しに結果を返すまでは圧縮しない
	CHECK(!conv.can_compact());
	conv.add_tool_result("call_1", "get_obs_status", "{\"fps\":60}");
	CHECK(!conv.has_pending_calls());
	conv.add_response(response_with_text("原因はキャプチャーの方法です"));
	CHECK(conv.request_items().size() == 5);

	// 約 80% で圧縮を勧める
	conv.set_last_input_tokens(kCompactionSuggestTokens - 1);
	CHECK(!conv.should_suggest_compaction());
	conv.set_last_input_tokens(kCompactionSuggestTokens);
	CHECK(conv.should_suggest_compaction());
	CHECK(conv.can_compact());

	const json summary_request = conv.build_summary_request(ModelConfig{}, "inst", build_tools_json());
	CHECK(summary_request["tool_choice"] == "none");
	CHECK(summary_request["input"].size() == 6);

	const size_t shown_before = conv.display().size();
	conv.apply_compaction("要約: 画面が映らない相談");
	// 画面の履歴は残し、要約を足す
	CHECK(conv.display().size() == shown_before + 1);
	CHECK(conv.display().back().kind == DisplayEntry::Kind::Summary);
	CHECK(!conv.should_suggest_compaction());

	// モデルには要約と、それ以降の会話だけを渡す
	auto items = conv.request_items();
	CHECK(items.size() == 1);
	CHECK(contains_word(items[0]["content"].get<std::string>(), "要約: 画面が映らない相談"));
	conv.add_user_message("まだ映らない");
	items = conv.request_items();
	CHECK(items.size() == 2);
	CHECK(items[1]["content"] == "まだ映らない");
	CHECK(!contains_word(json(items).dump(), "call_1"));

	// /clear は画面とモデルに渡す内容の両方を消す
	conv.clear();
	CHECK(conv.display().empty());
	CHECK(conv.request_items().empty());
	CHECK(!conv.can_compact());
}

// OBS 33 のモニタリングは有効と無効だけで、monitor_only には対応する値が無い
void test_monitoring_values()
{
	bool enabled = true;
	CHECK(monitoring_enabled_from_name(kMonitoringOff, enabled) && !enabled);
	CHECK(monitoring_enabled_from_name(kMonitoringAndOutput, enabled) && enabled);
	CHECK(!monitoring_enabled_from_name(kMonitoringOnly, enabled));
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
	test_mask_secrets();
	test_decide_change();
	test_proposal_book();
	test_conversation_compaction();
	test_monitoring_values();
	test_track_mixers();
	test_output_key_and_config_text();
	test_expected_video();
	test_log_file_selection();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
