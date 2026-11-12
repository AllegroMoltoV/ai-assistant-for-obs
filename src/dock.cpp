#include "dock.h"

#include "api-key-store.h"
#include "settings-dialog.h"

#include <obs-module.h>

#include <QApplication>
#include <QClipboard>
#include <QFrame>
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

// ドックの最小幅 (px)。候補のチップを 1 列に並べたとき、長い文言が見切れない幅
constexpr int kDockMinimumWidth = 300;

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
	// 縦に長いドックとして使う前提で、候補のチップや入力欄が見切れない幅を保つ
	setMinimumWidth(kDockMinimumWidth);
	// 話し手と提案を色で見分けられるようにする。色は OBS のテーマのパレットから取り、明暗の両方で読めるようにする
	setStyleSheet("QFrame#aiaUser { background: palette(alternate-base); border-radius: 6px; }"
		      "QFrame#aiaAssistant { background: palette(base); border-radius: 6px; }"
		      "QFrame#aiaProposal { border: 2px solid palette(highlight); border-radius: 6px; }"
		      "QFrame#aiaSummary { border: 1px dashed palette(mid); border-radius: 6px; }");

	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	history_ = new QWidget(scroll_);
	history_layout_ = new QVBoxLayout(history_);
	history_layout_->setAlignment(Qt::AlignTop);
	scroll_->setWidget(history_);
	// 新しい部品の配置が決まり、スクロールの範囲が広がった時点で末尾へ送る
	connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int max) {
		if (scroll_to_end_)
			scroll_->verticalScrollBar()->setValue(max);
	});

	status_label_ = new QLabel(this);
	cancel_button_ = new QPushButton(text("Dock.Cancel"), this);
	connect(cancel_button_, &QPushButton::clicked, this, [this] { session_->cancel(); });
	auto *status_row = new QHBoxLayout();
	status_row->addWidget(status_label_, 1);
	status_row->addWidget(cancel_button_);

	carry_over_button_ = new QPushButton(text("Carry.Run"), this);
	carry_over_button_->setToolTip(text("Carry.Tooltip"));
	connect(carry_over_button_, &QPushButton::clicked, this, [this] { session_->carry_over(); });

	limit_label_ = wrapped_label(text("Notice.LengthLimit"), this);
	limit_label_->setStyleSheet("color: #d9534f;");

	transient_label_ = new QLabel(this);
	transient_label_->setWordWrap(true);
	transient_label_->setStyleSheet("color: #d9534f;");
	transient_label_->hide();

	input_ = new ChatInput(text("Dock.InputPlaceholder"), this);
	input_->setMaximumHeight(80);
	input_->installEventFilter(this);
	gauge_ = new LengthGauge(this);
	gauge_->setFixedHeight(80);
	send_button_ = new QPushButton(text("Dock.Send"), this);
	connect(send_button_, &QPushButton::clicked, this, [this] { submit(); });
	auto *input_row = new QHBoxLayout();
	input_row->addWidget(input_, 1);
	input_row->addWidget(gauge_);
	input_row->addWidget(send_button_, 0, Qt::AlignBottom);

	// 応答の誤りの可能性と、AI に送るデータの範囲を入力欄の近くに示す。本文と同じ色と大きさで読めるようにする
	auto *disclaimer = wrapped_label(text("Dock.Disclaimer"), this);

	auto *settings_button = new QPushButton(text("Dock.Settings"), this);
	// 設定画面は非モーダルで開き、配信の操作を妨げない
	connect(settings_button, &QPushButton::clicked, this, [this] { (new SettingsDialog(window()))->show(); });
	auto *help_button = new QPushButton(QStringLiteral("?"), this);
	help_button->setToolTip(text("Dock.Help"));
	help_button->setFixedWidth(help_button->sizeHint().height());
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
	layout->addWidget(limit_label_);
	layout->addWidget(carry_over_button_);
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
	// コマンドは持たない (依頼者の決定)。入力はすべて相談として送る
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
	const Conversation &conversation = session_->conversation();
	const bool busy = state == SessionState::Waiting || state == SessionState::CarryingOver;
	if (state == SessionState::Waiting)
		status_label_->setText(text("Dock.Thinking"));
	else if (state == SessionState::CarryingOver)
		status_label_->setText(text("Carry.Running"));
	else if (state == SessionState::AwaitingApproval)
		status_label_->setText(text("Dock.AwaitingApproval"));
	else
		status_label_->clear();
	status_label_->setVisible(state != SessionState::Idle);
	cancel_button_->setVisible(busy);

	const bool at_limit = conversation.at_length_limit();
	gauge_->set_percent(conversation.length_percent());
	carry_over_button_->setVisible(state == SessionState::Idle && conversation.can_carry_over());
	limit_label_->setVisible(at_limit);
	input_->setEnabled(!at_limit);
	send_button_->setEnabled(state == SessionState::Idle && !at_limit);

	// 新しい内容が見えるよう、配置が決まってスクロールの範囲が広がった時点で末尾へ送る (rangeChanged)。
	// 範囲が変わらない作り直しにも備え、イベントの処理の後にも一度送る
	scroll_to_end_ = true;
	QTimer::singleShot(0, this, [this] {
		scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->maximum());
		QTimer::singleShot(200, this, [this] { scroll_to_end_ = false; });
	});
}

QWidget *Dock::build_empty_state()
{
	// できることとできないことを最初から見せ、悩みを一度で言葉にできなくても、選ぶだけで始められるようにする
	auto *box = new QWidget(history_);
	auto *layout = new QVBoxLayout(box);
	layout->addWidget(wrapped_label(text("Dock.Welcome"), box));
	layout->addWidget(build_help());
	// 縦に長いドックで見切れないよう、チップは 1 列に並べる
	for (const char *key : kChipKeys) {
		auto *chip = new QPushButton(text(key), box);
		const QString value = text(key);
		connect(chip, &QPushButton::clicked, this, [this, value] { send_text(value); });
		layout->addWidget(chip);
	}
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
		// 自分の発言も、言い直しや別の場所への貼り付けのためにコピーできるようにする
		auto *copy = new QPushButton(text("Dock.Copy"), frame);
		const QString body = utf8(entry.text);
		connect(copy, &QPushButton::clicked, this, [body] { QApplication::clipboard()->setText(body); });
		layout->addWidget(copy, 0, Qt::AlignRight);
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
		auto *title = new QLabel(text("Carry.SummaryTitle"), frame);
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
