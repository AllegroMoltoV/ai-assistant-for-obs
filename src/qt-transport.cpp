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

void QtTransport::send(const nlohmann::json &request, Done done)
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
	worker_ = std::thread([request, done = std::move(done), key = std::move(key), cancel, alive]() mutable {
		const OpenAiClient client(std::move(key), cancel.get());
		HttpResult result = client.post_responses(request);
		// 本文はログに書かず、処理の時間と状態だけを残す
		obs_log(LOG_INFO, "responses status=%ld elapsed_ms=%.0f%s", result.http_status, result.elapsed_ms,
			result.transport_error.empty() ? "" : " transport_error");
		if (cancel->load())
			return;
		post_to_main_thread([done = std::move(done), result = std::move(result), alive] {
			if (alive->load())
				done(result.http_status, result.body, result.transport_error);
		});
	});
}

} // namespace aia
