#pragma once

#include <nlohmann/json.hpp>

#include <vector>

namespace aia {

// 関数の道具の名前。削除の道具と、配信・録画の開始と停止の道具は持たない
inline constexpr const char *kToolGetObsStatus = "get_obs_status";
inline constexpr const char *kToolListScenesAndSources = "list_scenes_and_sources";
inline constexpr const char *kToolGetSourceDetails = "get_source_details";
inline constexpr const char *kToolReadLog = "read_log";
inline constexpr const char *kToolChangeSettings = "change_settings";
inline constexpr const char *kToolAddItem = "add_item";

// 取得の道具はすぐ実行し、操作の道具は利用者の承認を経て実行する
enum class ToolKind { Read, Operation };

struct FunctionToolInfo {
	const char *name;
	ToolKind kind;
};

const std::vector<FunctionToolInfo> &function_tools();

// 名前に一致する関数の道具を返す。無ければ nullptr
const FunctionToolInfo *find_function_tool(const std::string &name);

// Responses API の tools 配列。Web 検索と 6 つの関数を入れる
nlohmann::json build_tools_json();

} // namespace aia
