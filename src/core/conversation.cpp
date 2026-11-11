#include "conversation.h"

namespace aia {

namespace {

using nlohmann::json;

constexpr const char *kSummaryPrefix = "[Summary of the earlier conversation]\n";
constexpr const char *kSummaryRequest =
	"Summarize the conversation so far so that the troubleshooting can continue from the summary alone. "
	"Include the problem, the OBS state and log findings, the likely causes, what was proposed, applied, "
	"rejected or undone (with proposal ids), and open questions. Do not call any tools. "
	"Write the summary in the language the user writes in.";

} // namespace

void Conversation::add_user_message(const std::string &text)
{
	items_.push_back(user_message_item(text));
	display_.push_back({DisplayEntry::Kind::User, text});
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
}

void Conversation::add_proposal_entry(int proposal_id)
{
	DisplayEntry entry{DisplayEntry::Kind::Proposal};
	entry.proposal_id = proposal_id;
	display_.push_back(std::move(entry));
}

void Conversation::add_user_event(const std::string &text)
{
	items_.push_back(user_message_item(text));
	display_.push_back({DisplayEntry::Kind::Notice, text});
}

void Conversation::add_notice(const std::string &text)
{
	display_.push_back({DisplayEntry::Kind::Notice, text});
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

void Conversation::apply_compaction(const std::string &summary)
{
	summary_ = summary;
	items_.clear();
	pending_calls_.clear();
	last_input_tokens_ = 0;
	display_.push_back({DisplayEntry::Kind::Summary, summary});
}

void Conversation::clear()
{
	items_.clear();
	display_.clear();
	pending_calls_.clear();
	summary_.clear();
	last_input_tokens_ = 0;
}

} // namespace aia
