#include "proposals.h"

#include <algorithm>

namespace aia {

ChangeDecision decide_change(const Change &change, const OutputActivity &activity, OutputMode mode,
			     const std::string &old_value, const std::string &new_value)
{
	if (change.target != ChangeTarget::OutputSetting && change.target != ChangeTarget::VideoSetting)
		return ChangeDecision::Allowed;
	// 稼働中の判定を先に行う。稼働中は案内の項目でも書き換えないため、どちらでも結果は書き換えないことになる
	if (activity.any())
		return ChangeDecision::BlockedWhileActive;
	if (change.target == ChangeTarget::VideoSetting && !is_video_setting_key(change.key))
		return ChangeDecision::GuideOnly;
	if (classify_output_setting(mode, change.key, old_value, new_value) == OutputSettingClass::GuideOnly)
		return ChangeDecision::GuideOnly;
	return ChangeDecision::Allowed;
}

bool changes_audio_immediately(ChangeTarget target)
{
	switch (target) {
	case ChangeTarget::AudioMute:
	case ChangeTarget::AudioVolumeDb:
	case ChangeTarget::AudioMonitoring:
	case ChangeTarget::AudioSyncOffsetMs:
	case ChangeTarget::AudioTracks:
		return true;
	default:
		return false;
	}
}

bool changes_audio_immediately(AddKind kind)
{
	return kind == AddKind::NoiseSuppression || kind == AddKind::Gain || kind == AddKind::Compressor ||
	       kind == AddKind::ApplicationAudioCapture;
}

const char *proposal_state_name(ProposalState state)
{
	switch (state) {
	case ProposalState::Pending:
		return "pending";
	case ProposalState::Applied:
		return "applied";
	case ProposalState::Rejected:
		return "rejected_by_user";
	case ProposalState::Failed:
		return "failed";
	case ProposalState::Undone:
		return "undone_by_user";
	}
	return "unknown";
}

int ProposalBook::add(Proposal proposal)
{
	proposal.id = next_id_++;
	proposal.state = ProposalState::Pending;
	proposals_.push_back(std::move(proposal));
	return proposals_.back().id;
}

const Proposal *ProposalBook::find(int id) const
{
	const auto it =
		std::find_if(proposals_.begin(), proposals_.end(), [&](const Proposal &p) { return p.id == id; });
	return it == proposals_.end() ? nullptr : &*it;
}

Proposal *ProposalBook::find_mutable(int id)
{
	return const_cast<Proposal *>(static_cast<const ProposalBook *>(this)->find(id));
}

bool ProposalBook::mark_applied(int id)
{
	Proposal *p = find_mutable(id);
	if (!p || p->state != ProposalState::Pending)
		return false;
	p->state = ProposalState::Applied;
	return true;
}

bool ProposalBook::mark_rejected(int id)
{
	Proposal *p = find_mutable(id);
	if (!p || p->state != ProposalState::Pending)
		return false;
	p->state = ProposalState::Rejected;
	return true;
}

bool ProposalBook::mark_failed(int id, const std::string &error)
{
	Proposal *p = find_mutable(id);
	if (!p || p->state != ProposalState::Pending)
		return false;
	p->state = ProposalState::Failed;
	p->error = error;
	return true;
}

bool ProposalBook::can_undo(int id) const
{
	const Proposal *target = find(id);
	if (!target || target->state != ProposalState::Applied)
		return false;
	// 後から同じ対象を変えた提案が適用されたままなら、先にそちらを戻す必要がある
	for (const auto &later : proposals_) {
		if (later.id <= id || later.state != ProposalState::Applied)
			continue;
		for (const auto &a : target->items) {
			for (const auto &b : later.items) {
				if (a.target_id == b.target_id)
					return false;
			}
		}
	}
	return true;
}

bool ProposalBook::mark_undone(int id)
{
	if (!can_undo(id))
		return false;
	find_mutable(id)->state = ProposalState::Undone;
	return true;
}

std::string ProposalBook::tool_output(int id) const
{
	const Proposal *p = find(id);
	if (!p)
		return R"({"status":"unknown_proposal"})";
	nlohmann::json out = {{"proposal_id", p->id}, {"status", proposal_state_name(p->state)}};
	if (!p->error.empty())
		out["error"] = p->error;
	return out.dump();
}

} // namespace aia
