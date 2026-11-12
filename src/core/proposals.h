#pragma once

#include "change-request.h"
#include "output-settings-policy.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace aia {

// 出力の稼働状態。出力と映像の設定は、どれか 1 つでも稼働中なら変えない
struct OutputActivity {
	bool streaming = false;
	bool recording = false;
	bool replay_buffer = false;
	bool virtual_camera = false;
	// obs_video_active()。上の 4 つ以外の出力も含む
	bool video_active = false;

	bool any() const { return streaming || recording || replay_buffer || virtual_camera || video_active; }
};

enum class ChangeDecision {
	Allowed,
	// 出力が稼働中のため変えない
	BlockedWhileActive,
	// 公開の手段で反映できないため、設定画面の手順を案内する
	GuideOnly,
};

// 1 件の変更を提案に載せてよいか決める。old_value と new_value は録画形式の群の判定に使う
ChangeDecision decide_change(const Change &change, const OutputActivity &activity, OutputMode mode,
			     const std::string &old_value, const std::string &new_value);

// 適用した瞬間に配信や録画の音声が変わる変更のとき真
bool changes_audio_immediately(ChangeTarget target);
bool changes_audio_immediately(AddKind kind);

enum class ProposalState { Pending, Applied, Rejected, Failed, Undone };

// 利用者に見せる 1 件の変更
struct ProposalItem {
	// 何を変えるかの説明。例: "Micの音量"
	std::string label;
	// 承認の画面に出す、OBS の画面上の名前で表した値。適用と取り消しには Proposal::apply_data を使う
	std::string before;
	std::string after;
	// 同じ対象を変える提案の重なりを判定するための識別子。例: "audio_volume_db|Mic"
	std::string target_id;
	// 追加の提案のとき真。before は使わず、after に追加する内容の設定を入れる
	bool addition = false;
};

struct Proposal {
	int id = 0;
	std::string call_id;
	std::string tool;
	std::string reason;
	std::vector<ProposalItem> items;
	// 適用と取り消しに使うデータ。中身は OBS とつなぐ層が決める
	nlohmann::json apply_data;
	// 配信中か録画中に音声を変える提案のとき真。承認の画面で、視聴者にすぐ聞こえることを必ず示す
	bool live_audio = false;
	ProposalState state = ProposalState::Pending;
	std::string error;
};

// 操作の提案の台帳。承認は毎回の確認だけで、常に許可する設定は持たない
class ProposalBook {
public:
	// 提案を足して id を返す。状態は Pending
	int add(Proposal proposal);

	const Proposal *find(int id) const;
	const std::vector<Proposal> &all() const { return proposals_; }

	// Pending の提案だけを、適用・拒否・失敗に進められる
	bool mark_applied(int id);
	bool mark_rejected(int id);
	bool mark_failed(int id, const std::string &error);

	// 取り消せるのは Applied の提案で、後から同じ対象を変えて Applied のままの提案が無いとき
	bool can_undo(int id) const;
	bool mark_undone(int id);

	// 操作の道具の結果としてモデルに返す JSON の文字列
	std::string tool_output(int id) const;

	void clear() { proposals_.clear(); }

private:
	Proposal *find_mutable(int id);

	std::vector<Proposal> proposals_;
	int next_id_ = 1;
};

const char *proposal_state_name(ProposalState state);

} // namespace aia
