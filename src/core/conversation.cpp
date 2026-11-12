#include "conversation.h"

#include "moderation.h"

namespace aia {

namespace {

using nlohmann::json;

constexpr const char *kSummaryPrefix = "[Key points carried over from the previous conversation]\n";
constexpr const char *kSummaryRequest =
	"The conversation is getting too long. Write the key points so that the troubleshooting can continue in a "
	"new conversation from them alone. Include the problem, the OBS state and log findings, the likely causes, "
	"which settings were changed or added, and open questions. Keep it short. Do not call any tools. "
	"Write it in the language the user writes in.";

} // namespace

void Conversation::add_user_message(const std::string &text)
{
	items_.push_back(user_message_item(text));
	display_.push_back({DisplayEntry::Kind::User, text});
	display_of_item_[items_.size() - 1] = display_.size() - 1;
}

void Conversation::add_response(const ParsedResponse &response)
{
	for (const auto &item : response.output_items)
		items_.push_back(item);
	for (const auto &call : response.function_calls)
		pending_calls_.insert(call.call_id);
	if (!response.text.empty()) {
		DisplayEntry entry{DisplayEntry::Kind::Assistant, response.text};
		entry.citations = response.citations;
		display_.push_back(std::move(entry));
	}
}

void Conversation::add_tool_result(const std::string &call_id, const std::string &tool_name,
				   const std::string &sent_text)
{
	items_.push_back(function_call_output_item(call_id, sent_text));
	pending_calls_.erase(call_id);
	DisplayEntry entry{DisplayEntry::Kind::ToolResult, sent_text};
	entry.tool_name = tool_name;
	display_.push_back(std::move(entry));
	display_of_item_[items_.size() - 1] = display_.size() - 1;
}

void Conversation::add_proposal_entry(int proposal_id)
{
	DisplayEntry entry{DisplayEntry::Kind::Proposal};
	entry.proposal_id = proposal_id;
	display_.push_back(std::move(entry));
}

void Conversation::add_user_event(const std::string &model_text, const std::string &notice_key,
				  const std::string &detail)
{
	items_.push_back(user_message_item(model_text));
	add_notice(notice_key, detail);
}

void Conversation::add_notice(const std::string &notice_key, const std::string &detail)
{
	DisplayEntry entry{DisplayEntry::Kind::Notice, notice_key};
	entry.detail = detail;
	display_.push_back(std::move(entry));
}

std::vector<json> Conversation::request_items() const
{
	std::vector<json> result;
	if (!summary_.empty())
		result.push_back(user_message_item(kSummaryPrefix + summary_));
	result.insert(result.end(), items_.begin(), items_.end());
	return result;
}

json Conversation::build_summary_request(const ModelConfig &config, const std::string &instructions,
					 const json &tools) const
{
	std::vector<json> input = request_items();
	input.push_back(user_message_item(kSummaryRequest));
	json request = build_request(config, instructions, tools, input);
	request["tool_choice"] = "none";
	return request;
}

void Conversation::start_with_summary(const std::string &summary)
{
	clear();
	summary_ = summary;
	display_.push_back({DisplayEntry::Kind::Summary, summary});
}

void Conversation::clear()
{
	items_.clear();
	display_.clear();
	display_of_item_.clear();
	checked_upto_ = 0;
	pending_calls_.clear();
	summary_.clear();
	last_input_tokens_ = 0;
	length_notices_given_.clear();
}

int length_blocks(int percent)
{
	if (percent <= 0)
		return 0;
	if (percent >= 100)
		return 10;
	return percent / 10 + (percent % 10 != 0 ? 1 : 0);
}

int length_tier(int percent)
{
	if (percent >= 90)
		return 3;
	if (percent >= 70)
		return 2;
	if (percent >= 50)
		return 1;
	return 0;
}

int Conversation::length_percent() const
{
	if (last_input_tokens_ <= 0)
		return 0;
	const long long percent = last_input_tokens_ * 100 / kConversationLimitTokens;
	return percent > 100 ? 100 : static_cast<int>(percent);
}

bool Conversation::take_length_notice(int threshold_percent)
{
	if (length_percent() < threshold_percent || length_notices_given_.count(threshold_percent))
		return false;
	length_notices_given_.insert(threshold_percent);
	return true;
}

std::vector<Conversation::UncheckedItem> Conversation::unchecked_items() const
{
	std::vector<UncheckedItem> result;
	for (size_t i = checked_upto_; i < items_.size(); ++i) {
		const json &item = items_[i];
		// モデルの出力は OpenAI が返したものなので検査しない。送るのは利用者の発言と道具の結果
		if (item.value("role", "") == "user" && item.contains("content") && item["content"].is_string())
			result.push_back({i, item["content"].get<std::string>()});
		else if (item.value("type", "") == "function_call_output" && item.contains("output") &&
			 item["output"].is_string())
			result.push_back({i, item["output"].get<std::string>()});
	}
	return result;
}

void Conversation::withhold(size_t item_index)
{
	if (item_index >= items_.size())
		return;
	json &item = items_[item_index];
	const bool is_user = item.value("role", "") == "user";
	if (is_user)
		item["content"] = kWithheldUserText;
	else
		item["output"] = kWithheldToolOutput;
	// 利用者の発言は画面に残し、道具の結果は実際に送った文に差し替える
	const auto shown = display_of_item_.find(item_index);
	if (!is_user && shown != display_of_item_.end())
		display_[shown->second].text = kWithheldToolOutput;
}

} // namespace aia
