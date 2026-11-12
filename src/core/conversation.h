#pragma once

#include "responses.h"

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace aia {

// 会話の長さの分母。Web 検索のあいだに扱える量の上限 (https://developers.openai.com/api/docs/guides/tools-web-search)
inline constexpr long long kConversationLimitTokens = 128000;
// 会話の長さの段階 (%)。50・70・90 はゲージの色、75 と 90 は知らせ、50 は要点を引き継ぐボタンを出す境目。
// 依頼者の決定 (2026-11-12)
inline constexpr int kLengthCarryOverPercent = 50;
inline constexpr int kLengthNoticeLongPercent = 75;
inline constexpr int kLengthNoticeNearPercent = 90;
// 送信をできなくする境目。上限の手前で止め、次の応答で上限を超えないよう余裕を残す
inline constexpr int kLengthBlockPercent = 95;

// 会話の長さのゲージの、溜まったブロックの数 (0〜10)。1% でも使っていれば 1 つ目を点ける
int length_blocks(int percent);
// ゲージの色の段階。0: 50% 未満、1: 50% 以上、2: 70% 以上、3: 90% 以上
int length_tier(int percent);

// 画面に出す 1 件
struct DisplayEntry {
	enum class Kind {
		User,
		Assistant,
		// 道具の結果。AI に送った内容をそのまま持ち、利用者が確かめられるようにする
		ToolResult,
		// 操作の提案。中身は ProposalBook の proposal_id で引く
		Proposal,
		// 前の会話から引き継いだ要点
		Summary,
		// 誤りや状態の知らせ
		Notice,
	};
	Kind kind = Kind::Notice;
	// Notice では画面の文言のキー (例: Error.Http)。それ以外では本文
	std::string text;
	// Notice の補足。HTTP の状態や提案の番号など
	std::string detail;
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
	void add_notice(const std::string &notice_key, const std::string &detail = std::string());

	// 次の要求の input。前の会話から要点を引き継いだときは、要点を先頭に置く
	std::vector<nlohmann::json> request_items() const;
	const std::vector<DisplayEntry> &display() const { return display_; }

	void set_last_input_tokens(long long tokens) { last_input_tokens_ = tokens; }
	long long last_input_tokens() const { return last_input_tokens_; }
	// 会話の長さ (0〜100 %)。直前の応答の input_tokens を上限で割った値
	int length_percent() const;
	bool at_length_limit() const { return length_percent() >= kLengthBlockPercent; }
	bool can_carry_over() const
	{
		return length_percent() >= kLengthCarryOverPercent && !has_pending_calls() && !items_.empty();
	}

	// 関数の呼び出しに結果を返していない間は、要求を組めない
	bool has_pending_calls() const { return !pending_calls_.empty(); }

	// 要点の書き出しを頼む要求を作る。道具は渡すが呼ばせない
	nlohmann::json build_summary_request(const ModelConfig &config, const std::string &instructions,
					     const nlohmann::json &tools) const;
	// 要点を引き継いで新しい会話を始める。画面とモデルに渡す内容を消し、要点を画面とモデルの先頭に置く
	void start_with_summary(const std::string &summary);

	// 段階の知らせ (75 % と 90 %) を、まだ出していなければ出す。出したら真
	bool take_length_notice(int threshold_percent);

	// 画面とモデルに渡す内容の両方を消す
	void clear();

	// 送る前の検査の対象。まだ検査していない利用者の発言と道具の結果
	struct UncheckedItem {
		size_t item_index;
		std::string text;
	};
	std::vector<UncheckedItem> unchecked_items() const;
	void mark_all_checked() { checked_upto_ = items_.size(); }
	// 検査で問題ありとされた内容を、送らない旨の文に差し替える。画面の「送った内容」も差し替える
	void withhold(size_t item_index);

private:
	std::vector<nlohmann::json> items_;
	std::vector<DisplayEntry> display_;
	// items_ の番号から、その内容を見せる display_ の番号への対応
	std::map<size_t, size_t> display_of_item_;
	size_t checked_upto_ = 0;
	std::set<std::string> pending_calls_;
	std::string summary_;
	long long last_input_tokens_ = 0;
	std::set<int> length_notices_given_;
};

} // namespace aia
