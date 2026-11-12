#include "chat-input.h"

#include <QInputMethodEvent>

namespace aia {

ChatInput::ChatInput(const QString &placeholder, QWidget *parent) : QPlainTextEdit(parent), placeholder_(placeholder)
{
	setPlaceholderText(placeholder_);
}

void ChatInput::inputMethodEvent(QInputMethodEvent *event)
{
	const bool composing = !event->preeditString().isEmpty();
	setPlaceholderText(composing ? QString() : placeholder_);
	QPlainTextEdit::inputMethodEvent(event);
}

} // namespace aia
