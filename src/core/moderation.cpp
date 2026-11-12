#include "moderation.h"

namespace aia {

nlohmann::json build_moderation_request(const std::vector<std::string> &texts)
{
	return {{"model", kModerationModel}, {"input", texts}};
}

ModerationResult parse_moderation(long http_status, const nlohmann::json &body, size_t expected_count)
{
	ModerationResult result;
	if (http_status != 200 || !body.is_object())
		return result;
	const auto results = body.find("results");
	if (results == body.end() || !results->is_array() || results->size() != expected_count)
		return result;
	for (const auto &item : *results) {
		const auto flagged = item.find("flagged");
		if (flagged == item.end() || !flagged->is_boolean())
			return ModerationResult{};
		result.flagged.push_back(flagged->get<bool>());
	}
	result.ok = true;
	return result;
}

} // namespace aia
