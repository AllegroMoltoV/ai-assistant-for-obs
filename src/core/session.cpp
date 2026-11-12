#include "session.h"

#include "instructions.h"
#include "moderation.h"
#include "tool-definitions.h"

namespace aia {

namespace {

using nlohmann::json;

std::string error_json(const std::string &message)
{
	return json({{"status", "error"}, {"error", message}}).dump();
}

} // namespace

Session::Session(Transport &transport, ToolHost &host, ModelConfig config)
	: transport_(transport),
	  host_(host),
	  config_(std::move(config)),
	  tools_(build_tools_json())
{
	set_locale(std::string());
}

void Session::set_locale(const std::string &locale)
{
	instructions_ = assistant_instructions() + "\n\n" + language_instruction(locale);
}

void Session::changed()
{
	if (on_changed_)
		on_changed_();
}

void Session::send_user_message(const std::string &text)
{
	if (state_ != SessionState::Idle || text.empty() || conversation_.at_length_limit())
		return;
	conversation_.add_user_message(text);
	steps_ = 0;
	start_request();
}

void Session::start_request()
{
	if (steps_ >= kMaxStepsPerTurn) {
		conversation_.add_notice(kNoticeTooManySteps);
		state_ = SessionState::Idle;
		changed();
		return;
	}
	// 道具の往復の途中でも、上限の手前に達したら送らない。次の応答で上限を超えるのを防ぐ
	if (conversation_.at_length_limit()) {
		conversation_.add_notice(kNoticeLengthLimit);
		state_ = SessionState::Idle;
		changed();
		return;
	}
	++steps_;
	state_ = SessionState::Waiting;
	changed();
	check_then([this] {
		const int generation = generation_;
		const json request =
			build_request(config_, instructions_, tools_, conversation_.request_items());
		transport_.send(Endpoint::Responses, request,
				[this, generation](long status, const json &body, const std::string &error) {
					if (generation != generation_)
						return;
					handle_response(status, body, error);
				});
	});
}

void Session::check_then(std::function<void()> next)
{
	const auto unchecked = conversation_.unchecked_items();
	if (unchecked.empty()) {
		next();
		return;
	}
	std::vector<std::string> texts;
	for (const auto &item : unchecked)
		texts.push_back(item.text);
	const int generation = generation_;
	transport_.send(Endpoint::Moderations, build_moderation_request(texts),
			[this, generation, unchecked, next](long status, const json &body, const std::string &error) {
				if (generation != generation_)
					return;
				const ModerationResult result = parse_moderation(status, body, unchecked.size());
				if (!result.ok) {
					conversation_.add_notice(kNoticeModerationFailed,
								 error.empty() ? std::to_string(status) : error);
					state_ = SessionState::Idle;
					changed();
					return;
				}
				for (size_t i = 0; i < unchecked.size(); ++i) {
					if (!result.flagged[i])
						continue;
					conversation_.withhold(unchecked[i].item_index);
					conversation_.add_notice(kNoticeWithheld);
				}
				conversation_.mark_all_checked();
				next();
			});
}

void Session::add_error_notice(long http_status, const ParsedResponse &parsed, const std::string &transport_error)
{
	if (!transport_error.empty() && http_status == 0) {
		conversation_.add_notice(kNoticeNetworkError, transport_error);
		return;
	}
	if (http_status == 401 || parsed.error_code == "invalid_api_key") {
		conversation_.add_notice(kNoticeInvalidKey);
		return;
	}
	std::string detail = std::to_string(http_status);
	if (!parsed.error_message.empty())
		detail += " " + parsed.error_message;
	conversation_.add_notice(kNoticeHttpError, detail);
}

void Session::handle_response(long http_status, const json &body, const std::string &transport_error)
{
	const ParsedResponse parsed = parse_response(http_status, body);
	if (!parsed.ok) {
		add_error_notice(http_status, parsed, transport_error);
		state_ = SessionState::Idle;
		changed();
		return;
	}

	conversation_.add_response(parsed);
	if (parsed.usage.input_tokens > 0)
		conversation_.set_last_input_tokens(parsed.usage.input_tokens);
	if (parsed.status == "incomplete" && parsed.incomplete_reason == "max_output_tokens")
		conversation_.add_notice(kNoticeIncomplete);
	else if (parsed.status == "incomplete")
		conversation_.add_notice(kNoticeIncompleteOther, parsed.incomplete_reason);
	else if (parsed.text.empty() && parsed.function_calls.empty())
		conversation_.add_notice(kNoticeEmptyReply);
	// いきなり上限に達したと感じさせないよう、段階ごとに一度だけ知らせる
	if (conversation_.take_length_notice(kLengthNoticeNearPercent)) {
		conversation_.take_length_notice(kLengthNoticeLongPercent);
		conversation_.add_notice(kNoticeLengthNear);
	} else if (conversation_.take_length_notice(kLengthNoticeLongPercent)) {
		conversation_.add_notice(kNoticeLengthLong);
	}

	for (const auto &call : parsed.function_calls)
		run_function_call(call);

	continue_if_ready();
}

void Session::run_function_call(const FunctionCall &call)
{
	const FunctionToolInfo *tool = find_function_tool(call.name);
	if (!tool) {
		conversation_.add_tool_result(call.call_id, call.name, error_json("unknown tool"));
		return;
	}
	if (tool->kind == ToolKind::Read) {
		conversation_.add_tool_result(call.call_id, call.name, host_.run_read_tool(call.name, call.arguments));
		return;
	}

	Parsed<Proposal> prepared;
	if (call.name == kToolChangeSettings) {
		const auto request = parse_change_settings(call.arguments);
		if (!request.ok) {
			conversation_.add_tool_result(call.call_id, call.name, error_json(request.error));
			return;
		}
		prepared = host_.prepare_change(request.value);
		prepared.value.reason = request.value.reason;
	} else if (call.name == kToolAddItem) {
		const auto request = parse_add_item(call.arguments);
		if (!request.ok) {
			conversation_.add_tool_result(call.call_id, call.name, error_json(request.error));
			return;
		}
		prepared = host_.prepare_add(request.value);
		prepared.value.reason = request.value.reason;
	} else {
		const auto request = parse_remove_item(call.arguments);
		if (!request.ok) {
			conversation_.add_tool_result(call.call_id, call.name, error_json(request.error));
			return;
		}
		prepared = host_.prepare_remove(request.value);
		prepared.value.reason = request.value.reason;
	}
	if (!prepared.ok) {
		conversation_.add_tool_result(call.call_id, call.name, error_json(prepared.error));
		return;
	}
	prepared.value.call_id = call.call_id;
	prepared.value.tool = call.name;
	const int id = proposals_.add(std::move(prepared.value));
	conversation_.add_proposal_entry(id);
}

void Session::continue_if_ready()
{
	if (conversation_.has_pending_calls()) {
		state_ = SessionState::AwaitingApproval;
		changed();
		return;
	}
	// 直前の応答が道具を呼んでいたら、結果を渡して続ける。呼んでいなければ利用者の番
	const auto items = conversation_.request_items();
	const bool last_is_tool_output = !items.empty() && items.back().value("type", "") == "function_call_output";
	if (last_is_tool_output) {
		start_request();
		return;
	}
	state_ = SessionState::Idle;
	changed();
}

void Session::resolve_proposal(int proposal_id)
{
	const Proposal *proposal = proposals_.find(proposal_id);
	if (!proposal)
		return;
	conversation_.add_tool_result(proposal->call_id, proposal->tool, proposals_.tool_output(proposal_id));
	continue_if_ready();
}

void Session::approve(int proposal_id)
{
	const Proposal *proposal = proposals_.find(proposal_id);
	if (state_ != SessionState::AwaitingApproval || !proposal || proposal->state != ProposalState::Pending)
		return;
	std::string error;
	if (host_.apply(*proposal, error)) {
		proposals_.mark_applied(proposal_id);
	} else {
		proposals_.mark_failed(proposal_id, error);
		conversation_.add_notice(kNoticeApplyFailed, error);
	}
	resolve_proposal(proposal_id);
}

void Session::reject(int proposal_id)
{
	const Proposal *proposal = proposals_.find(proposal_id);
	if (state_ != SessionState::AwaitingApproval || !proposal || proposal->state != ProposalState::Pending)
		return;
	proposals_.mark_rejected(proposal_id);
	resolve_proposal(proposal_id);
}

void Session::carry_over()
{
	if (state_ != SessionState::Idle || !conversation_.can_carry_over())
		return;
	state_ = SessionState::CarryingOver;
	changed();
	check_then([this] {
		const int generation = generation_;
		const json request = conversation_.build_summary_request(config_, instructions_, tools_);
		transport_.send(Endpoint::Responses, request,
				[this, generation](long status, const json &body, const std::string &error) {
					if (generation != generation_)
						return;
					handle_summary(status, body, error);
				});
	});
}

void Session::handle_summary(long http_status, const json &body, const std::string &transport_error)
{
	const ParsedResponse parsed = parse_response(http_status, body);
	if (!parsed.ok || parsed.text.empty()) {
		if (parsed.ok)
			conversation_.add_notice(kNoticeCarryOverFailed);
		else
			add_error_notice(http_status, parsed, transport_error);
		state_ = SessionState::Idle;
		changed();
		return;
	}
	// 新しい会話は新しいセッションのため、実行の記録も消す
	conversation_.start_with_summary(parsed.text);
	proposals_.clear();
	steps_ = 0;
	conversation_.add_notice(kNoticeCarriedOver);
	state_ = SessionState::Idle;
	changed();
}

void Session::cancel()
{
	if (state_ != SessionState::Waiting && state_ != SessionState::CarryingOver)
		return;
	++generation_;
	transport_.cancel();
	conversation_.add_notice(kNoticeCancelled);
	state_ = SessionState::Idle;
	changed();
}

void Session::show_help()
{
	conversation_.add_notice(kNoticeHelp);
	changed();
}

void Session::clear()
{
	++generation_;
	if (state_ == SessionState::Waiting || state_ == SessionState::CarryingOver)
		transport_.cancel();
	conversation_.clear();
	proposals_.clear();
	steps_ = 0;
	state_ = SessionState::Idle;
	changed();
}

} // namespace aia
