#pragma once

#include "core/session.h"

#include <atomic>
#include <memory>
#include <thread>

namespace aia {

// Session の送り先。作業のスレッドで OpenAI に送り、結果を Qt のメインスレッドで done に渡す。
// 一度に送るのは 1 件だけ (Session がそう呼ぶ)
class QtTransport : public Transport {
public:
	QtTransport() = default;
	~QtTransport() override;

	void send(Endpoint endpoint, const nlohmann::json &request, Done done) override;
	void cancel() override;

private:
	void join();

	std::thread worker_;
	std::shared_ptr<std::atomic<bool>> cancel_;
	// 破棄の後に届いた結果を捨てるため、作業のスレッドと共有する
	std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

} // namespace aia
