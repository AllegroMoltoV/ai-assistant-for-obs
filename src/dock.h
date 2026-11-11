#pragma once

#include "core/session.h"
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
	explicit Dock(ToolHost &host, QWidget *parent = nullptr);
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
	std::unique_ptr<QtTransport> transport_;
	std::unique_ptr<Session> session_;

	QScrollArea *scroll_ = nullptr;
	QWidget *history_ = nullptr;
	QVBoxLayout *history_layout_ = nullptr;
	QLabel *status_label_ = nullptr;
	QPushButton *cancel_button_ = nullptr;
	QWidget *compact_bar_ = nullptr;
	QLabel *transient_label_ = nullptr;
	QPlainTextEdit *input_ = nullptr;
	QPushButton *send_button_ = nullptr;
};

} // namespace aia
