#pragma once

#include "chat-input.h"
#include "core/session.h"
#include "length-gauge.h"
#include "qt-transport.h"

#include <QWidget>

#include <memory>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace aia {

// 相談のチャットのドック。表示は Session の会話の履歴から毎回作り直す
class Dock : public QWidget {
public:
	// transport を省くと OpenAI へ送る QtTransport を使う。開発用の画面の確認の道具は、模した応答を返す送り先を渡す
	explicit Dock(ToolHost &host, std::unique_ptr<Transport> transport = nullptr, QWidget *parent = nullptr);
	~Dock() override;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void rebuild();
	void submit();
	void send_text(const QString &text);
	void show_transient(const QString &text);

	QWidget *build_empty_state();
	QWidget *build_entry(const DisplayEntry &entry);
	QWidget *build_assistant(const DisplayEntry &entry);
	QWidget *build_tool_result(const DisplayEntry &entry);
	QWidget *build_proposal(int proposal_id);
	QWidget *build_notice(const DisplayEntry &entry);
	QWidget *build_help();

	// 送り先は Session より後に破棄する。破棄の後に届いた結果を捨てるため
	std::unique_ptr<Transport> transport_;
	std::unique_ptr<Session> session_;

	QScrollArea *scroll_ = nullptr;
	QWidget *history_ = nullptr;
	QVBoxLayout *history_layout_ = nullptr;
	QLabel *status_label_ = nullptr;
	QPushButton *cancel_button_ = nullptr;
	// 会話の長さが約 50% に達してから出す、要点を引き継いで新しい会話を始めるボタン
	QPushButton *carry_over_button_ = nullptr;
	// 上限の手前で送信を止めたときの理由
	QLabel *limit_label_ = nullptr;
	QLabel *transient_label_ = nullptr;
	ChatInput *input_ = nullptr;
	LengthGauge *gauge_ = nullptr;
	QPushButton *send_button_ = nullptr;
	// 作り直した後、配置が決まってから末尾へ送るための印
	bool scroll_to_end_ = false;
};

} // namespace aia
