#include "qt-transport.h"

#include "api-key-store.h"
#include "main-thread.h"
#include "openai-client.h"

#include <obs-module.h>
#include <plugin-support.h>

namespace aia {

QtTransport::~QtTransport()
{
	alive_->store(false);
	cancel();
	// プラグインの解放の後に libcurl を呼ばないよう、作業のスレッドの終わりを待つ。中断の印で数百ミリ秒で戻る
	join();
}

void QtTransport::join()
{
	if (worker_.joinable())
		worker_.join();
}

void QtTransport::cancel()
{
	if (cancel_)
		cancel_->store(true);
}

void QtTransport::send(Endpoint endpoint, const nlohmann::json &request, Done done)
{
	const bool moderation = endpoint == Endpoint::Moderations;
	start(
		moderation ? "moderations" : "responses",
		[request, moderation](const OpenAiClient &client, const std::shared_ptr<std::atomic<bool>> &,
				      const std::shared_ptr<std::atomic<bool>> &) {
			return moderation ? client.post_moderations(request) : client.post_responses(request);
		},
		std::move(done));
}

void QtTransport::send_stream(const nlohmann::json &request, Delta delta, Done done)
{
	start(
		"responses_stream",
		[request, delta = std::move(delta)](const OpenAiClient &client,
						    const std::shared_ptr<std::atomic<bool>> &cancel,
						    const std::shared_ptr<std::atomic<bool>> &alive) {
			// 文の差分はメインスレッドへ届いた順に移す。結果の done も同じキューに入るため、差分より後に届く
			return client.post_responses_stream(request, [&delta, cancel, alive](const std::string &text) {
				if (cancel->load())
					return;
				post_to_main_thread([delta, text, alive] {
					if (alive->load())
						delta(text);
				});
			});
		},
		std::move(done));
}

void QtTransport::start(const char *label, Call call, Done done)
{
	// 前の要求の作業のスレッドは、結果を渡し終えているか中断済み
	join();

	std::string key;
	if (load_saved_api_key(key) != ApiKeyStatus::Ok) {
		post_to_main_thread([done = std::move(done)] { done(0, nlohmann::json(), "api key not set"); });
		return;
	}

	cancel_ = std::make_shared<std::atomic<bool>>(false);
	auto cancel = cancel_;
	auto alive = alive_;
	worker_ = std::thread([label, call = std::move(call), done = std::move(done), key = std::move(key), cancel,
			       alive]() mutable {
		const OpenAiClient client(std::move(key), cancel.get());
		HttpResult result = call(client, cancel, alive);
		// 本文はログに書かず、処理の時間と状態だけを残す。通信の失敗の理由 (libcurl の説明) と、
		// 応答の状態と途中で切れた理由は、画面に何も出なかった場合の原因を追えるよう残す
		std::string detail;
		if (!result.transport_error.empty())
			detail += " transport_error=\"" + result.transport_error + "\"";
		// 型の違う値で例外を投げないよう、文字列のときだけ読む
		const auto string_at = [](const nlohmann::json &object, const char *key) {
			const auto found = object.find(key);
			return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
		};
		if (result.body.is_object()) {
			const std::string status = string_at(result.body, "status");
			if (!status.empty())
				detail += " response_status=" + status;
			const auto incomplete = result.body.find("incomplete_details");
			if (incomplete != result.body.end() && incomplete->is_object())
				detail += " incomplete_reason=" + string_at(*incomplete, "reason");
		}
		if (cancel->load())
			detail += " cancelled";
		obs_log(LOG_INFO, "%s status=%ld elapsed_ms=%.0f%s", label, result.http_status, result.elapsed_ms,
			detail.c_str());
		if (cancel->load())
			return;
		post_to_main_thread([done = std::move(done), result = std::move(result), alive] {
			if (alive->load())
				done(result.http_status, result.body, result.transport_error);
		});
	});
}

} // namespace aia
