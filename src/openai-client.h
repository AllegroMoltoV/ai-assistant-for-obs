#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <string>

namespace aia {

struct HttpResult {
	long http_status = 0;
	double elapsed_ms = 0.0;
	// JSON として読めなかった場合は discarded
	nlohmann::json body;
	// libcurl の失敗か、本文が JSON でない場合の説明。成功時は空
	std::string transport_error;
};

// v1/responses へ同期で要求を送る。呼び出し元のスレッドで待つため、UI のスレッドからは呼ばない。
// cancel が真になると、libcurl の進捗のコールバックで転送を中断する
class OpenAiClient {
public:
	OpenAiClient(std::string api_key, const std::atomic<bool> *cancel = nullptr)
		: api_key_(std::move(api_key)),
		  cancel_(cancel)
	{
	}

	HttpResult post_responses(const nlohmann::json &request) const;
	// 送る前の検査。v1/moderations は無料 (https://developers.openai.com/api/docs/guides/moderation)
	HttpResult post_moderations(const nlohmann::json &request) const;

private:
	HttpResult post(const char *url, const nlohmann::json &request) const;

	std::string api_key_;
	const std::atomic<bool> *cancel_;
};

} // namespace aia
