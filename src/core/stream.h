#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace aia {

// Responses API の stream: true の応答 (Server-Sent Events) を、届いた分ずつ読む。
// 文の差分 (response.output_text.delta) はその都度 on_delta に渡し、最後の応答の全体
// (response.completed / incomplete / failed の response) を、stream でない応答と同じ形で返す。
// 認証の失敗などの誤りは SSE ではなく JSON の本文で返るため、その場合は本文をそのまま返す
class StreamReader {
public:
	using Delta = std::function<void(const std::string &text)>;

	explicit StreamReader(Delta on_delta) : on_delta_(std::move(on_delta)) {}

	// 届いた分を読む。区切りの途中で切れた行は、次に届いた分とつなぐ
	void feed(const char *data, size_t size);
	// 届き終わった後の本文。最後の応答が無ければ、届いた全体を JSON として読む。読めなければ discarded
	nlohmann::json finish();

private:
	void handle_line(const std::string &line);
	void dispatch();

	Delta on_delta_;
	std::string pending_;
	std::string data_;
	// SSE でない本文 (誤りの JSON) のために、届いた全体を持つ。上限を超えた分は捨てる
	std::string raw_;
	nlohmann::json final_;
	bool has_final_ = false;
};

} // namespace aia
