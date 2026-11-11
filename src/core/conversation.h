#pragma once

#include "responses.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace aia {

// 圧縮を勧める量。Web 検索のあいだの上限 128k の約 80%。親課題の design の仮決め
inline constexpr long long kCompactionSuggestTokens = 100000;

// 画面に出す 1 件。圧縮しても消さない
struct DisplayEntry {
	enum class Kind {
		User,
		Assistant,
		// 道具の結果。AI に送った内容をそのまま持ち、利用者が確かめられるようにする
		ToolResult,
		// 操作の提案。中身は ProposalBook の proposal_id で引く
		Proposal,
		// 圧縮の要約
		Summary,
		// 誤りや状態の知らせ
		Notice,
	};
	Kind kind = Kind::Notice;
	std::string text;
	std::string tool_name;
	std::vector<Citation> citations;
	int proposal_id = 0;
};

// 会話の状態。モデルに送る内容 (items) と画面の履歴 (display) を別に持つ
class Conversation {
public:
	void add_user_message(const std::string &text);
	// 応答の出力をすべて items に足し、本文があれば画面に出す。未回答の関数の呼び出しを記録する
	void add_response(const ParsedResponse &response);
	// 道具の結果を items に足す。sent_text は AI に送ったとおりの内容
	void add_tool_result(const std::string &call_id, const std::string &tool_name, const std::string &sent_text);
	void add_proposal_entry(int proposal_id);
	// 利用者が取り消しなどを行ったことを、次の要求でモデルに伝える
	void add_user_event(const std::string &text);
	void add_notice(const std::string &text);

	// 次の要求の input。圧縮した後は、要約と、それ以降の内容だけを返す
	std::vector<nlohmann::json> request_items() const;
	const std::vector<DisplayEntry> &display() const { return display_; }

	void set_last_input_tokens(long long tokens) { last_input_tokens_ = tokens; }
	long long last_input_tokens() const { return last_input_tokens_; }
	bool should_suggest_compaction() const { return last_input_tokens_ >= kCompactionSuggestTokens; }

	// 関数の呼び出しに結果を返していない間は、要求を組めないため圧縮もしない
	bool has_pending_calls() const { return !pending_calls_.empty(); }
	bool can_compact() const { return !has_pending_calls() && !items_.empty(); }

	// 要約を頼む要求を作る。道具は渡すが呼ばせない
	nlohmann::json build_summary_request(const ModelConfig &config, const std::string &instructions,
					     const nlohmann::json &tools) const;
	// 要約で置き換える。画面の履歴は残し、要約を画面にも出す
	void apply_compaction(const std::string &summary);

	// 画面とモデルに渡す内容の両方を消す
	void clear();

private:
	std::vector<nlohmann::json> items_;
	std::vector<DisplayEntry> display_;
	std::set<std::string> pending_calls_;
	std::string summary_;
	long long last_input_tokens_ = 0;
};

} // namespace aia
