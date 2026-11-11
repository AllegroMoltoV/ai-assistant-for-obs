#pragma once

#include "conversation.h"
#include "proposals.h"
#include "responses.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace aia {

// 要求の送り先。終わったら done を呼ぶ。プラグインでは作業のスレッドで送り、done をメインスレッドで呼ぶ
class Transport {
public:
	virtual ~Transport() = default;
	// transport_error は libcurl の失敗や本文が JSON でない場合の説明。成功時は空
	using Done =
		std::function<void(long http_status, const nlohmann::json &body, const std::string &transport_error)>;
	virtual void send(const nlohmann::json &request, Done done) = 0;
	virtual void cancel() = 0;
};

// 道具の実行先。プラグインでは obs-tools の関数を呼ぶ
class ToolHost {
public:
	virtual ~ToolHost() = default;
	// 取得の道具を実行し、AI に送る文字列を返す
	virtual std::string run_read_tool(const std::string &name, const std::string &arguments) = 0;
	virtual Parsed<Proposal> prepare_change(const ChangeRequest &request) = 0;
	virtual Parsed<Proposal> prepare_add(const AddRequest &request) = 0;
	virtual bool apply(const Proposal &proposal, std::string &error) = 0;
	virtual bool undo(const Proposal &proposal, std::string &error) = 0;
};

enum class SessionState {
	Idle,
	// 応答を待っている
	Waiting,
	// 操作の提案への利用者の判断を待っている
	AwaitingApproval,
	// 要約の応答を待っている
	Compacting,
};

// 画面の知らせの種類。文言は画面の側で翻訳する
inline constexpr const char *kNoticeHttpError = "Error.Http";
inline constexpr const char *kNoticeNetworkError = "Error.Network";
inline constexpr const char *kNoticeInvalidKey = "Error.InvalidKey";
inline constexpr const char *kNoticeTooManySteps = "Error.TooManySteps";
inline constexpr const char *kNoticeIncomplete = "Error.Incomplete";
inline constexpr const char *kNoticeCompacted = "Notice.Compacted";
inline constexpr const char *kNoticeCompactFailed = "Error.CompactFailed";
inline constexpr const char *kNoticeUndone = "Notice.Undone";
inline constexpr const char *kNoticeUndoFailed = "Error.UndoFailed";
inline constexpr const char *kNoticeApplyFailed = "Error.ApplyFailed";
inline constexpr const char *kNoticeCancelled = "Notice.Cancelled";
// できること・できないこと・質問の例 (/help)。画面の側で中身を描く
inline constexpr const char *kNoticeHelp = "Notice.Help";

// 1 回の発話から続けて送る要求の上限。道具の呼び出しが止まらない場合に費用が膨らむのを防ぐ
inline constexpr int kMaxStepsPerTurn = 12;

// 相談の会話を進める。メインスレッドだけから呼ぶ
class Session {
public:
	Session(Transport &transport, ToolHost &host, ModelConfig config = ModelConfig{});

	void send_user_message(const std::string &text);
	void approve(int proposal_id);
	void reject(int proposal_id);
	void undo(int proposal_id);
	// 手動の圧縮 (/compact) と、勧めのボタンからの圧縮
	void compact();
	// 画面とモデルに渡す内容の両方を消す (/clear)
	void clear();
	void cancel();
	// /help の表示を画面の履歴に足す。モデルには送らない
	void show_help();

	SessionState state() const { return state_; }
	const Conversation &conversation() const { return conversation_; }
	const ProposalBook &proposals() const { return proposals_; }
	const ModelConfig &config() const { return config_; }

	// 状態や表示が変わるたびに呼ぶ
	void set_on_changed(std::function<void()> fn) { on_changed_ = std::move(fn); }

private:
	void start_request();
	void handle_response(long http_status, const nlohmann::json &body, const std::string &transport_error);
	void handle_summary(long http_status, const nlohmann::json &body, const std::string &transport_error);
	void run_function_call(const FunctionCall &call);
	void resolve_proposal(int proposal_id);
	void continue_if_ready();
	void add_error_notice(long http_status, const ParsedResponse &parsed, const std::string &transport_error);
	void changed();

	Transport &transport_;
	ToolHost &host_;
	ModelConfig config_;
	nlohmann::json tools_;
	Conversation conversation_;
	ProposalBook proposals_;
	SessionState state_ = SessionState::Idle;
	int steps_ = 0;
	// 世代の番号。消去や取り消しの後に届いた古い応答を捨てる
	int generation_ = 0;
	std::function<void()> on_changed_;
};

} // namespace aia
