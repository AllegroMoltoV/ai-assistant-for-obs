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
class QTimer;
class QVBoxLayout;

namespace aia {

// 相談のチャットのドック。表示は Session の会話の履歴から毎回作り直す
class Dock : public QWidget {
public:
	// locale は OBS の表示言語 (例: ja-JP)。AI の回答の言語の基準にする。
	// transport を省くと OpenAI へ送る QtTransport を使う。開発用の画面の確認の道具は、模した応答を返す送り先を渡す
	Dock(ToolHost &host, const std::string &locale, std::unique_ptr<Transport> transport = nullptr,
	     QWidget *parent = nullptr);
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
	QWidget *build_help(QWidget *parent);
	// ヘルプ文を別の窓で出す (「?」のボタン)
	void show_help_window();

	// 送り先は Session より後に破棄する。破棄の後に届いた結果を捨てるため
	std::unique_ptr<Transport> transport_;
	std::unique_ptr<Session> session_;

	QScrollArea *scroll_ = nullptr;
	QWidget *history_ = nullptr;
	QVBoxLayout *history_layout_ = nullptr;
	QLabel *status_label_ = nullptr;
	QPushButton *cancel_button_ = nullptr;
	// 会話の長さが約 50% に達してから出す、会話を要約して新しい会話を始めるボタン
	QPushButton *carry_over_button_ = nullptr;
	// 上限の手前で送信を止めたときの理由
	QLabel *limit_label_ = nullptr;
	QLabel *transient_label_ = nullptr;
	ChatInput *input_ = nullptr;
	LengthGauge *gauge_ = nullptr;
	QPushButton *send_button_ = nullptr;
	// 作り直した後、配置が決まってから末尾へ送るための印
	bool scroll_to_end_ = false;
	// 応答を待つ間、状態の文の末尾の点を 1 つずつ増やし、読み込み中であることを伝える
	QTimer *busy_timer_ = nullptr;
	QString busy_text_;
	int busy_dots_ = 0;
	// 会話の欄の中身が見切れない幅を、ドックの最小幅にする
	void update_minimum_width();
	// 届いている途中の回答の文を、履歴の末尾の枠に出す
	void update_streaming();
	// 届いている途中の回答の文を出すラベル。作り直しで消える
	QLabel *stream_label_ = nullptr;
};

} // namespace aia
