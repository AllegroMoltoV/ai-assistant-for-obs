#include "dock.h"

#include "api-key-store.h"
#include "settings-dialog.h"

#include <obs-module.h>

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <set>

namespace aia {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString utf8(const std::string &value)
{
	return QString::fromStdString(value);
}

// 候補のチップ。調査で多かった困りごとの順。キーは data/locale の Chip.*
constexpr const char *kChipKeys[] = {"Chip.NoVideo", "Chip.NoGameAudio", "Chip.NoMic", "Chip.Echo",
				     "Chip.Stutter", "Chip.Dropped",     "Chip.Noise"};

QLabel *wrapped_label(const QString &value, QWidget *parent)
{
	auto *label = new QLabel(value, parent);
	label->setWordWrap(true);
	label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
	return label;
}

QFrame *card(QWidget *parent, const char *object_name)
{
	auto *frame = new QFrame(parent);
	frame->setObjectName(object_name);
	frame->setFrameShape(QFrame::StyledPanel);
	return frame;
}

QString proposal_state_text(ProposalState state)
{
	switch (state) {
	case ProposalState::Pending:
		return text("Proposal.Pending");
	case ProposalState::Applied:
		return text("Proposal.Applied");
	case ProposalState::Rejected:
		return text("Proposal.Rejected");
	case ProposalState::Failed:
		return text("Proposal.Failed");
	case ProposalState::Undone:
		return text("Proposal.Undone");
	}
	return QString();
}

} // namespace

Dock::Dock(ToolHost &host, QWidget *parent)
	: QWidget(parent),
	  transport_(std::make_unique<QtTransport>()),
	  session_(std::make_unique<Session>(*transport_, host))
{
	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	history_ = new QWidget(scroll_);
	history_layout_ = new QVBoxLayout(history_);
	history_layout_->setAlignment(Qt::AlignTop);
	scroll_->setWidget(history_);

	status_label_ = new QLabel(this);
	cancel_button_ = new QPushButton(text("Dock.Cancel"), this);
	connect(cancel_button_, &QPushButton::clicked, this, [this] { session_->cancel(); });
	auto *status_row = new QHBoxLayout();
	status_row->addWidget(status_label_, 1);
	status_row->addWidget(cancel_button_);

	// 手本と同じく、量が上限の約 80% に達したら理由と実行のボタンを付けて圧縮を勧める
	compact_bar_ = card(this, "aiaCompactBar");
	auto *compact_label = wrapped_label(text("Compact.Suggest"), compact_bar_);
	auto *compact_button = new QPushButton(text("Compact.Run"), compact_bar_);
	connect(compact_button, &QPushButton::clicked, this, [this] { session_->compact(); });
	auto *compact_layout = new QHBoxLayout(compact_bar_);
	compact_layout->addWidget(compact_label, 1);
	compact_layout->addWidget(compact_button);

	transient_label_ = new QLabel(this);
	transient_label_->setWordWrap(true);
	transient_label_->setStyleSheet("color: #d9534f;");
	transient_label_->hide();

	input_ = new QPlainTextEdit(this);
	input_->setPlaceholderText(text("Dock.InputPlaceholder"));
	input_->setMaximumHeight(80);
	input_->installEventFilter(this);
	send_button_ = new QPushButton(text("Dock.Send"), this);
	connect(send_button_, &QPushButton::clicked, this, [this] { submit(); });
	auto *input_row = new QHBoxLayout();
	input_row->addWidget(input_, 1);
	input_row->addWidget(send_button_, 0, Qt::AlignBottom);

	// 応答の誤りの可能性と、AI に送るデータの範囲を入力欄の近くに示す
	auto *disclaimer = wrapped_label(text("Dock.Disclaimer"), this);
	disclaimer->setStyleSheet("color: palette(mid); font-size: 11px;");

	auto *settings_button = new QPushButton(text("Dock.Settings"), this);
	// 設定画面は非モーダルで開き、配信の操作を妨げない
	connect(settings_button, &QPushButton::clicked, this, [this] { (new SettingsDialog(window()))->show(); });
	auto *help_button = new QPushButton(text("Dock.Help"), this);
	connect(help_button, &QPushButton::clicked, this, [this] { session_->show_help(); });
	auto *clear_button = new QPushButton(text("Dock.Clear"), this);
	connect(clear_button, &QPushButton::clicked, this, [this] { session_->clear(); });
	auto *tool_row = new QHBoxLayout();
	tool_row->addWidget(help_button);
	tool_row->addWidget(clear_button);
	tool_row->addStretch(1);
	tool_row->addWidget(settings_button);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(scroll_, 1);
	layout->addLayout(status_row);
	layout->addWidget(compact_bar_);
	layout->addWidget(transient_label_);
	layout->addLayout(input_row);
	layout->addWidget(disclaimer);
	layout->addLayout(tool_row);

	session_->set_on_changed([this] { rebuild(); });
	rebuild();
}

Dock::~Dock()
{
	// 破棄の途中で表示を作り直さない
	session_->set_on_changed(nullptr);
}

bool Dock::eventFilter(QObject *watched, QEvent *event)
{
	// Enter で送り、Shift+Enter で改行する
	if (watched == input_ && event->type() == QEvent::KeyPress) {
		auto *key = static_cast<QKeyEvent *>(event);
		if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
		    !(key->modifiers() & Qt::ShiftModifier)) {
			submit();
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void Dock::show_transient(const QString &value)
{
	transient_label_->setText(value);
	transient_label_->setVisible(!value.isEmpty());
}

void Dock::submit()
{
	const QString value = input_->toPlainText().trimmed();
	if (value.isEmpty())
		return;
	// 手本のスラッシュのコマンド。モデルには送らない
	if (value == QStringLiteral("/help")) {
		input_->clear();
		session_->show_help();
		return;
	}
	if (value == QStringLiteral("/compact")) {
		input_->clear();
		if (!session_->conversation().can_compact())
			show_transient(text("Compact.Nothing"));
		session_->compact();
		return;
	}
	if (value == QStringLiteral("/clear")) {
		input_->clear();
		session_->clear();
		return;
	}
	send_text(value);
}

void Dock::send_text(const QString &value)
{
	if (session_->state() != SessionState::Idle)
		return;
	if (!api_key_saved()) {
		show_transient(text("Error.NoApiKey"));
		return;
	}
	show_transient(QString());
	input_->clear();
	session_->send_user_message(value.toStdString());
}

void Dock::rebuild()
{
	while (QLayoutItem *item = history_layout_->takeAt(0)) {
		if (QWidget *widget = item->widget())
			widget->deleteLater();
		delete item;
	}

	const auto &entries = session_->conversation().display();
	if (entries.empty())
		history_layout_->addWidget(build_empty_state());
	for (const auto &entry : entries)
		history_layout_->addWidget(build_entry(entry));

	const SessionState state = session_->state();
	const bool busy = state == SessionState::Waiting || state == SessionState::Compacting;
	if (state == SessionState::Waiting)
		status_label_->setText(text("Dock.Thinking"));
	else if (state == SessionState::Compacting)
		status_label_->setText(text("Compact.Running"));
	else if (state == SessionState::AwaitingApproval)
		status_label_->setText(text("Dock.AwaitingApproval"));
	else
		status_label_->clear();
	status_label_->setVisible(state != SessionState::Idle);
	cancel_button_->setVisible(busy);
	compact_bar_->setVisible(state == SessionState::Idle && session_->conversation().should_suggest_compaction());
	send_button_->setEnabled(state == SessionState::Idle);

	// 新しい内容が見えるよう、配置が終わってから末尾へ送る
	QTimer::singleShot(0, this,
			   [this] { scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum()); });
}

QWidget *Dock::build_empty_state()
{
	// 悩みを一度で言葉にできなくても、選ぶだけで始められるようにする
	auto *box = new QWidget(history_);
	auto *layout = new QVBoxLayout(box);
	layout->addWidget(wrapped_label(text("Dock.Welcome"), box));
	auto *grid = new QGridLayout();
	int index = 0;
	for (const char *key : kChipKeys) {
		auto *chip = new QPushButton(text(key), box);
		const QString value = text(key);
		connect(chip, &QPushButton::clicked, this, [this, value] { send_text(value); });
		grid->addWidget(chip, index / 2, index % 2);
		++index;
	}
	layout->addLayout(grid);
	return box;
}

QWidget *Dock::build_entry(const DisplayEntry &entry)
{
	switch (entry.kind) {
	case DisplayEntry::Kind::User: {
		auto *frame = card(history_, "aiaUser");
		auto *layout = new QVBoxLayout(frame);
		auto *who = new QLabel(text("Dock.You"), frame);
		who->setStyleSheet("font-weight: bold;");
		layout->addWidget(who);
		auto *label = wrapped_label(utf8(entry.text), frame);
		label->setTextFormat(Qt::PlainText);
		layout->addWidget(label);
		return frame;
	}
	case DisplayEntry::Kind::Assistant:
		return build_assistant(entry);
	case DisplayEntry::Kind::ToolResult:
		return build_tool_result(entry);
	case DisplayEntry::Kind::Proposal:
		return build_proposal(entry.proposal_id);
	case DisplayEntry::Kind::Summary: {
		auto *frame = card(history_, "aiaSummary");
		auto *layout = new QVBoxLayout(frame);
		auto *title = new QLabel(text("Compact.SummaryTitle"), frame);
		title->setStyleSheet("font-weight: bold;");
		layout->addWidget(title);
		auto *label = wrapped_label(utf8(entry.text), frame);
		label->setTextFormat(Qt::MarkdownText);
		label->setOpenExternalLinks(true);
		layout->addWidget(label);
		return frame;
	}
	case DisplayEntry::Kind::Notice:
		return entry.text == kNoticeHelp ? build_help() : build_notice(entry);
	}
	return new QWidget(history_);
}

QWidget *Dock::build_assistant(const DisplayEntry &entry)
{
	auto *frame = card(history_, "aiaAssistant");
	auto *layout = new QVBoxLayout(frame);
	auto *who = new QLabel(text("Dock.Assistant"), frame);
	who->setStyleSheet("font-weight: bold;");
	layout->addWidget(who);
	auto *label = wrapped_label(utf8(entry.text), frame);
	label->setTextFormat(Qt::MarkdownText);
	label->setOpenExternalLinks(true);
	layout->addWidget(label);

	// Web 検索を使った応答には出典のリンクを付ける。同じ URL は 1 度だけ
	std::set<std::string> seen;
	QStringList links;
	for (const auto &citation : entry.citations) {
		if (!seen.insert(citation.url).second)
			continue;
		const QString title = citation.title.empty() ? utf8(citation.url) : utf8(citation.title);
		links << QStringLiteral("<a href=\"%1\">%2</a>")
				 .arg(utf8(citation.url).toHtmlEscaped(), title.toHtmlEscaped());
	}
	if (!links.isEmpty()) {
		auto *sources = new QLabel(
			text("Dock.Sources") + QStringLiteral(" ") + links.join(QStringLiteral(" / ")), frame);
		sources->setTextFormat(Qt::RichText);
		sources->setOpenExternalLinks(true);
		sources->setWordWrap(true);
		layout->addWidget(sources);
	}

	auto *copy = new QPushButton(text("Dock.Copy"), frame);
	const QString body = utf8(entry.text);
	connect(copy, &QPushButton::clicked, this, [body] { QApplication::clipboard()->setText(body); });
	layout->addWidget(copy, 0, Qt::AlignRight);
	return frame;
}

QWidget *Dock::build_tool_result(const DisplayEntry &entry)
{
	// AI に送った内容を、利用者が会話の中で確かめられるようにする。普段は畳んでおく
	auto *box = new QWidget(history_);
	auto *layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	auto *toggle = new QToolButton(box);
	// 道具の内部の名前ではなく、data/locale の Tool.<名前> の表示名を見せる
	const std::string tool_key = "Tool." + entry.tool_name;
	toggle->setText(text("Dock.SentData").arg(text(tool_key.c_str())));
	toggle->setCheckable(true);
	toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	toggle->setArrowType(Qt::RightArrow);
	auto *content = new QPlainTextEdit(box);
	content->setReadOnly(true);
	content->setPlainText(utf8(entry.text));
	content->setMaximumHeight(200);
	content->hide();
	connect(toggle, &QToolButton::toggled, box, [toggle, content](bool on) {
		content->setVisible(on);
		toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});
	layout->addWidget(toggle);
	layout->addWidget(content);
	return box;
}

QWidget *Dock::build_proposal(int proposal_id)
{
	const Proposal *proposal = session_->proposals().find(proposal_id);
	auto *frame = card(history_, "aiaProposal");
	auto *layout = new QVBoxLayout(frame);
	if (!proposal)
		return frame;

	auto *title = new QLabel(text("Proposal.Title").arg(proposal->id), frame);
	title->setStyleSheet("font-weight: bold;");
	layout->addWidget(title);
	if (!proposal->reason.empty())
		layout->addWidget(wrapped_label(utf8(proposal->reason), frame));
	// モデルの指示文に頼らず、配信中・録画中の音声の変更では必ず注意を出す
	if (proposal->live_audio && proposal->state == ProposalState::Pending) {
		auto *warning = wrapped_label(text("Proposal.LiveAudio"), frame);
		warning->setStyleSheet("color: #d9534f;");
		layout->addWidget(warning);
	}

	// 変える前と後の値の差分を見せる。追加なら追加する内容を見せる
	for (const auto &item : proposal->items) {
		const QString line =
			item.addition
				? text("Proposal.AddLine").arg(utf8(item.label), utf8(item.after))
				: text("Proposal.ChangeLine").arg(utf8(item.label), utf8(item.before), utf8(item.after));
		auto *label = wrapped_label(line, frame);
		label->setTextFormat(Qt::PlainText);
		layout->addWidget(label);
	}

	QString state = proposal_state_text(proposal->state);
	if (proposal->state == ProposalState::Failed && !proposal->error.empty())
		state += QStringLiteral(": ") + utf8(proposal->error);
	layout->addWidget(new QLabel(state, frame));

	auto *buttons = new QHBoxLayout();
	buttons->addStretch(1);
	const SessionState session_state = session_->state();
	if (proposal->state == ProposalState::Pending && session_state == SessionState::AwaitingApproval) {
		auto *reject = new QPushButton(text("Proposal.Reject"), frame);
		auto *approve = new QPushButton(text("Proposal.Approve"), frame);
		connect(reject, &QPushButton::clicked, this, [this, proposal_id] { session_->reject(proposal_id); });
		connect(approve, &QPushButton::clicked, this, [this, proposal_id] { session_->approve(proposal_id); });
		buttons->addWidget(reject);
		buttons->addWidget(approve);
	}
	if (session_->proposals().can_undo(proposal_id) && session_state == SessionState::Idle) {
		auto *undo = new QPushButton(text("Proposal.Undo"), frame);
		connect(undo, &QPushButton::clicked, this, [this, proposal_id] { session_->undo(proposal_id); });
		buttons->addWidget(undo);
	}
	layout->addLayout(buttons);
	return frame;
}

QWidget *Dock::build_notice(const DisplayEntry &entry)
{
	QString value = text(entry.text.c_str());
	if (!entry.detail.empty())
		value += QStringLiteral(" (") + utf8(entry.detail) + QStringLiteral(")");
	auto *label = wrapped_label(value, history_);
	label->setTextFormat(Qt::PlainText);
	label->setStyleSheet("color: palette(mid);");
	return label;
}

QWidget *Dock::build_help()
{
	// できることと、できないことを分けて見せる
	auto *frame = card(history_, "aiaHelp");
	auto *layout = new QVBoxLayout(frame);
	auto *label = wrapped_label(text("Help.Body"), frame);
	label->setTextFormat(Qt::MarkdownText);
	layout->addWidget(label);
	return frame;
}

} // namespace aia
