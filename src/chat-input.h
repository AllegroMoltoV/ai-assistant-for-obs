#pragma once

#include <QPlainTextEdit>

namespace aia {

// 相談の入力欄。日本語などの変換中は、確定前の文字が文書に入らず欄が空とみなされ、
// 案内の文字 (プレースホルダー) と重なって表示される。変換中だけ案内の文字を外す
class ChatInput : public QPlainTextEdit {
public:
	explicit ChatInput(const QString &placeholder, QWidget *parent = nullptr);

protected:
	void inputMethodEvent(QInputMethodEvent *event) override;

private:
	QString placeholder_;
};

} // namespace aia
