#pragma once

#include "conversation.h"
#include "proposals.h"
#include "responses.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace aia {

enum class Endpoint {
	Responses,
	// 送る前の検査
	Moderations,
};

// 要求の送り先。終わったら done を呼ぶ。プラグインでは作業のスレッドで送り、done をメインスレッドで呼ぶ
class Transport {
public:
	virtual ~Transport() = default;
	// transport_error は libcurl の失敗や本文が JSON でない場合の説明。成功時は空
	using Done =
		std::function<void(long http_status, const nlohmann::json &body, const std::string &transport_error)>;
	virtual void send(Endpoint endpoint, const nlohmann::json &request, Done done) = 0;
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
	virtual Parsed<Proposal> prepare_remove(const RemoveRequest &request) = 0;
	virtual bool apply(const Proposal &proposal, std::string &error) = 0;
};

enum class SessionState {
	Idle,
	// 応答を待っている
	Waiting,
	// 操作の提案への利用者の判断を待っている
	AwaitingApproval,
	// 要点を引き継ぐための応答を待っている
	CarryingOver,
};

// 画面の知らせの種類。文言は画面の側で翻訳する
inline constexpr const char *kNoticeHttpError = "Error.Http";
inline constexpr const char *kNoticeNetworkError = "Error.Network";
inline constexpr const char *kNoticeInvalidKey = "Error.InvalidKey";
inline constexpr const char *kNoticeTooManySteps = "Error.TooManySteps";
// 出力の上限で回答が切れた
inline constexpr const char *kNoticeIncomplete = "Error.Incomplete";
// 出力の上限以外の理由で回答が切れた。理由を補足に出す
inline constexpr const char *kNoticeIncompleteOther = "Error.IncompleteOther";
// 応答に本文も道具の呼び出しも無かった。画面に何も出ないまま終わるのを防ぐ
inline constexpr const char *kNoticeEmptyReply = "Error.EmptyReply";
inline constexpr const char *kNoticeCarriedOver = "Notice.CarriedOver";
inline constexpr const char *kNoticeCarryOverFailed = "Error.CarryOverFailed";
// 会話の長さの段階の知らせ (約 75 % と約 90 %) と、上限の手前で送信を止めた知らせ
inline constexpr const char *kNoticeLengthLong = "Notice.LengthLong";
inline constexpr const char *kNoticeLengthNear = "Notice.LengthNear";
inline constexpr const char *kNoticeLengthLimit = "Notice.LengthLimit";
inline constexpr const char *kNoticeApplyFailed = "Error.ApplyFailed";
inline constexpr const char *kNoticeCancelled = "Notice.Cancelled";
// 送る前の検査で問題ありとされ、その内容を送らなかった
inline constexpr const char *kNoticeWithheld = "Notice.Withheld";
// 送る前の検査ができなかったため、何も送らなかった
inline constexpr const char *kNoticeModerationFailed = "Error.ModerationFailed";

// 1 回の発話から続けて送る要求の上限。道具の呼び出しが止まらない場合に費用が膨らむのを防ぐ
inline constexpr int kMaxStepsPerTurn = 12;

// 相談の会話を進める。メインスレッドだけから呼ぶ
class Session {
public:
	Session(Transport &transport, ToolHost &host, ModelConfig config = ModelConfig{});

	void send_user_message(const std::string &text);
	void approve(int proposal_id);
	void reject(int proposal_id);
	// 要点を引き継いで新しい会話を始める。会話の長さが約 50 % に達してから使える
	void carry_over();
	// 画面とモデルに渡す内容と、実行の記録を消す (「新しい会話」のボタン)
	void clear();
	void cancel();

	// 回答の言語の基準にする OBS の表示言語 (例: ja-JP)
	void set_locale(const std::string &locale);

	SessionState state() const { return state_; }
	const Conversation &conversation() const { return conversation_; }
	const ProposalBook &proposals() const { return proposals_; }
	const ModelConfig &config() const { return config_; }

	// 状態や表示が変わるたびに呼ぶ
	void set_on_changed(std::function<void()> fn) { on_changed_ = std::move(fn); }

private:
	void start_request();
	// まだ検査していない利用者の発言と道具の結果を検査し、問題ありとされた内容を差し替えてから next を呼ぶ。
	// 検査できなければ何も送らずに止める
	void check_then(std::function<void()> next);
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
	// 共通の指示文に回答の言語の指示を足したもの
	std::string instructions_;
	Conversation conversation_;
	ProposalBook proposals_;
	SessionState state_ = SessionState::Idle;
	int steps_ = 0;
	// 世代の番号。消去や中止の後に届いた古い応答を捨てる
	int generation_ = 0;
	std::function<void()> on_changed_;
};

} // namespace aia
