#include "obs-tool-host.h"

#include "core/tool-definitions.h"
#include "obs-tools.h"

namespace aia {

std::string ObsToolHost::run_read_tool(const std::string &name, const std::string &arguments)
{
	if (name == kToolGetObsStatus)
		return read_obs_status();
	if (name == kToolListScenesAndSources)
		return read_scenes_and_sources();
	if (name == kToolReadLog)
		return read_obs_log();
	if (name == kToolGetSourceDetails) {
		const auto args = nlohmann::json::parse(arguments, nullptr, false);
		const std::string source = args.is_object() ? args.value("source", "") : std::string();
		if (source.empty())
			return R"({"status":"error","error":"source must not be empty"})";
		return read_source_details(source);
	}
	return R"({"status":"error","error":"unknown tool"})";
}

Parsed<Proposal> ObsToolHost::prepare_change(const ChangeRequest &request)
{
	return prepare_change_proposal(request);
}

Parsed<Proposal> ObsToolHost::prepare_add(const AddRequest &request)
{
	return prepare_add_proposal(request);
}

Parsed<Proposal> ObsToolHost::prepare_remove(const RemoveRequest &request)
{
	return prepare_remove_proposal(request);
}

bool ObsToolHost::apply(const Proposal &proposal, std::string &error)
{
	return apply_proposal(proposal, error);
}

} // namespace aia
