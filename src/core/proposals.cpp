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

std::string ProposalBook::tool_output(int id) const
{
	const Proposal *p = find(id);
	if (!p)
		return R"({"status":"unknown_proposal"})";
	nlohmann::json out = {{"proposal_id", p->id}, {"status", proposal_state_name(p->state)}};
	if (!p->error.empty())
		out["error"] = p->error;
	nlohmann::json items = nlohmann::json::array();
	for (const auto &item : p->items) {
		nlohmann::json entry = {{"label", item.label}};
		switch (item.kind) {
		case ProposalItemKind::Change:
			entry["operation"] = "change";
			entry["before"] = item.before;
			entry["after"] = item.after;
			entry["applies"] = apply_timing_name(item.timing);
			break;
		case ProposalItemKind::Add:
			entry["operation"] = "add";
			entry["settings"] = item.after;
			break;
		case ProposalItemKind::Remove:
			entry["operation"] = "remove";
			break;
		}
		if (item.record.is_object())
			entry.update(item.record);
		items.push_back(std::move(entry));
	}
	out["items"] = std::move(items);
	return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::vector<ApplyTiming> notable_timings(const Proposal &proposal)
{
	std::vector<ApplyTiming> timings;
	for (const auto &item : proposal.items) {
		if (item.timing == ApplyTiming::Immediate ||
		    std::find(timings.begin(), timings.end(), item.timing) != timings.end())
			continue;
		timings.push_back(item.timing);
	}
	return timings;
}

} // namespace aia
