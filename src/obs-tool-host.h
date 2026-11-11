#pragma once

#include "core/session.h"

namespace aia {

// Session の道具の実行先。obs-tools の関数をメインスレッドで呼ぶ
class ObsToolHost : public ToolHost {
public:
	std::string run_read_tool(const std::string &name, const std::string &arguments) override;
	Parsed<Proposal> prepare_change(const ChangeRequest &request) override;
	Parsed<Proposal> prepare_add(const AddRequest &request) override;
	bool apply(const Proposal &proposal, std::string &error) override;
	bool undo(const Proposal &proposal, std::string &error) override;
};

} // namespace aia
