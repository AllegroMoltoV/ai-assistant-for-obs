#include "openai-client.h"

#include "core/stream.h"

#include <curl/curl.h>

#include <chrono>
#include <functional>

namespace aia {

namespace {

constexpr const char *kResponsesUrl = "https://api.openai.com/v1/responses";
constexpr const char *kModerationsUrl = "https://api.openai.com/v1/moderations";
// Web 検索と推論を含む応答は数十秒かかることがあるため、流用元の 30 秒から延ばす
constexpr long kTimeoutSeconds = 180;
// 接続のタイムアウトも全体と同じにする。OpenAI のサーバーは応答の途中で TLS の再交渉を求め、OBS 同梱の libcurl 8.12.1
// (Schannel) は再交渉に接続のタイムアウトを当てる。10 秒にすると、要求の開始から 10 秒を過ぎた応答が
// "Timeout was reached" で必ず失敗した (docs/logs/2026-11-12-tls-renegotiation-timeout.md)
constexpr long kConnectTimeoutSeconds = kTimeoutSeconds;

// 届いた本文を受け取り先の関数に渡す
size_t write_body(char *data, size_t size, size_t count, void *user)
{
	(*static_cast<const std::function<void(const char *, size_t)> *>(user))(data, size *count);
	return size * count;
}

// 0 以外を返すと libcurl が転送を中断する
int check_cancel(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto *cancel = static_cast<const std::atomic<bool> *>(user);
	return cancel && cancel->load() ? 1 : 0;
}

} // namespace

HttpResult OpenAiClient::post_responses(const nlohmann::json &request) const
{
	return post_json(kResponsesUrl, request);
}

HttpResult OpenAiClient::post_moderations(const nlohmann::json &request) const
{
	return post_json(kModerationsUrl, request);
}

HttpResult OpenAiClient::post_responses_stream(const nlohmann::json &request,
					       const std::function<void(const std::string &)> &on_delta) const
{
	nlohmann::json body = request;
	body["stream"] = true;
	StreamReader reader(on_delta);
	HttpResult result =
		post(kResponsesUrl, body, [&reader](const char *data, size_t size) { reader.feed(data, size); });
	if (!result.transport_error.empty())
		return result;
	result.body = reader.finish();
	if (result.body.is_discarded() || !result.body.is_object())
		result.transport_error = "invalid json";
	return result;
}

HttpResult OpenAiClient::post_json(const char *url, const nlohmann::json &request) const
{
	std::string response;
	HttpResult result =
		post(url, request, [&response](const char *data, size_t size) { response.append(data, size); });
	if (!result.transport_error.empty())
		return result;
	result.body = nlohmann::json::parse(response, nullptr, false);
	if (result.body.is_discarded() || !result.body.is_object())
		result.transport_error = "invalid json";
	return result;
}

HttpResult OpenAiClient::post(const char *url, const nlohmann::json &request,
			      const std::function<void(const char *, size_t)> &sink) const
{
	HttpResult result;

	CURL *curl = curl_easy_init();
	if (!curl) {
		result.transport_error = "curl_easy_init";
		return result;
	}

	const std::string body = request.dump();
	curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_body);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, const_cast<std::function<void(const char *, size_t)> *>(&sink));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTimeoutSeconds);
	if (cancel_) {
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &check_cancel);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(cancel_));
	}

	const auto started = std::chrono::steady_clock::now();
	const CURLcode rc = curl_easy_perform(curl);
	result.elapsed_ms =
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_status);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK)
		result.transport_error = curl_easy_strerror(rc);
	return result;
}

} // namespace aia
