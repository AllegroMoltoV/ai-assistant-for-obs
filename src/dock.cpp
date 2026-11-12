#include "dock.h"

#include "api-key-store.h"
#include "settings-dialog.h"

#include <obs-module.h>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
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

#include <algorithm>
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
	}
	return QString();
}

} // namespace

Dock::Dock(ToolHost &host, const std::string &locale, std::unique_ptr<Transport> transport, QWidget *parent)
	: QWidget(parent),
	  transport_(transport ? std::move(transport) : std::make_unique<QtTransport>()),
	  session_(std::make_unique<Session>(*transport_, host))
{
	// 縦に長いドックとして使う前提で、候補のチップや入力欄が見切れない幅を保つ
	setMinimumWidth(kDockMinimumWidth);
	// 話し手と提案を色で見分けられるようにする。色は OBS のテーマのパレットから取り、明暗の両方で読めるようにする。
	// alternate-base と base は多くのテーマでほぼ同じ色のため、利用者の発言には強調色を薄く重ねる
	QColor user_tint = palette().color(QPalette::Highlight);
	user_tint.setAlpha(70);
	setStyleSheet(QString("QFrame#aiaUser { background: rgba(%1, %2, %3, %4); border-radius: 6px; }")
			      .arg(user_tint.red())
			      .arg(user_tint.green())
			      .arg(user_tint.blue())
			      .arg(user_tint.alpha()) +
		      "QFrame#aiaAssistant { background: palette(base); border-radius: 6px; }"
		      "QFrame#aiaProposal { border: 2px solid palette(highlight); border-radius: 6px; }"
		      "QFrame#aiaSummary { border: 1px dashed palette(mid); border-radius: 6px; }");

	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	// 横のスクロールバーは出さない。中身はすべて折り返し、折り返せない幅はドックの最小幅で確保する
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	history_ = new QWidget(scroll_);
	history_layout_ = new QVBoxLayout(history_);
	scroll_->setWidget(history_);
	// 新しい部品の配置が決まり、スクロールの範囲が広がった時点で末尾へ送る
	connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int max) {
		if (scroll_to_end_)
			scroll_->verticalScrollBar()->setValue(max);
	});

	status_label_ = new QLabel(this);
	busy_timer_ = new QTimer(this);
	busy_timer_->setInterval(400);
	connect(busy_timer_, &QTimer::timeout, this, [this] {
		busy_dots_ = busy_dots_ % 3 + 1;
		status_label_->setText(busy_text_ + QString(busy_dots_, QLatin1Char('.')));
	});
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
	// 入力できる場所だと分かるよう、テーマの背景のまま強調色の枠で囲む (依頼者の決定)
	input_->setStyleSheet("QPlainTextEdit { border: 1px solid palette(highlight); }");
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
	// 幅はテーマの余白を含めた推奨の幅に任せる。高さに合わせて固定すると、テーマの余白で「?」が見切れる
	auto *help_button = new QPushButton(QStringLiteral("?"), this);
	help_button->setToolTip(text("Dock.Help"));
	// 会話の中に出すと候補のボタンが消えるため、別の窓で出す (依頼者の決定)
	connect(help_button, &QPushButton::clicked, this, [this] { show_help_window(); });
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

	// AI の回答を、利用者が OBS で選んだ表示言語に合わせる
	session_->set_locale(locale);
	session_->set_on_changed([this] { rebuild(); });
	session_->set_on_streaming([this] { update_streaming(); });
	rebuild();
}

Dock::~Dock()
{
	// 破棄の途中で表示を作り直さない
	session_->set_on_changed(nullptr);
	session_->set_on_streaming(nullptr);
}

void Dock::update_streaming()
{
	const std::string &value = session_->streaming_text();
	if (value.empty())
		return;
	// 最初の文が届いたら、AI アシスタントの発言の枠を履歴の末尾 (伸びる余白の前) に足す
	if (!stream_label_) {
		auto *frame = card(history_, "aiaAssistant");
		auto *layout = new QVBoxLayout(frame);
		auto *who = new QLabel(text("Dock.Assistant"), frame);
		who->setStyleSheet("font-weight: bold;");
		layout->addWidget(who);
		stream_label_ = wrapped_label(QString(), frame);
		stream_label_->setTextFormat(Qt::MarkdownText);
		stream_label_->setOpenExternalLinks(true);
		layout->addWidget(stream_label_);
		history_layout_->insertWidget(history_layout_->count() - 1, frame);
	}
	// 末尾を見ている間だけ、文が伸びるのに合わせて末尾へ送る。読み返している途中では動かさない
	QScrollBar *bar = scroll_->verticalScrollBar();
	scroll_to_end_ = bar->value() >= bar->maximum() - 40;
	stream_label_->setText(utf8(value));
	// 文が届き始めたら、考えています… の表示は止める。中止のボタンは残す
	busy_timer_->stop();
	status_label_->clear();
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
	stream_label_ = nullptr;

	const auto &entries = session_->conversation().display();
	if (entries.empty())
		history_layout_->addWidget(build_empty_state());
	for (const auto &entry : entries)
		history_layout_->addWidget(build_entry(entry));
	// 上に詰めるため、末尾に伸びる余白を置く。配置の上寄せの指定 (setAlignment) は、
	// 折り返す文の高さを実際の幅ではなく既定の幅で求めさせ、狭いドックで文が見切れるため使わない
	history_layout_->addStretch(1);

	const SessionState state = session_->state();
	const Conversation &conversation = session_->conversation();
	const bool busy = state == SessionState::Waiting || state == SessionState::CarryingOver;
	if (busy) {
		const QString value = text(state == SessionState::Waiting ? "Dock.Thinking" : "Carry.Running");
		if (value != busy_text_ || !busy_timer_->isActive()) {
			busy_text_ = value;
			busy_dots_ = 1;
			status_label_->setText(busy_text_ + QStringLiteral("."));
			busy_timer_->start();
		}
	} else {
		busy_timer_->stop();
		busy_text_.clear();
		if (state == SessionState::AwaitingApproval)
			status_label_->setText(text("Dock.AwaitingApproval"));
		else
			status_label_->clear();
	}
	status_label_->setVisible(state != SessionState::Idle);
	cancel_button_->setVisible(busy);
	// 作り直しの間も届いている途中の文を見せ続ける
	update_streaming();

	const bool at_limit = conversation.at_length_limit();
	gauge_->set_percent(conversation.length_percent());
	carry_over_button_->setVisible(state == SessionState::Idle && conversation.can_carry_over());
	limit_label_->setVisible(at_limit);
	input_->setEnabled(!at_limit);
	send_button_->setEnabled(state == SessionState::Idle && !at_limit);

	// 新しい内容が見えるよう、配置が決まってスクロールの範囲が広がった時点で末尾へ送る (rangeChanged)。
	// 範囲が変わらない作り直しにも備え、イベントの処理の後にも一度送る。
	// 空の会話では、ヘルプ文が最初に目に入るよう先頭を見せる
	const bool to_end = !entries.empty();
	scroll_to_end_ = to_end;
	QTimer::singleShot(0, this, [this, to_end] {
		update_minimum_width();
		QScrollBar *bar = scroll_->verticalScrollBar();
		bar->setValue(to_end ? bar->maximum() : bar->minimum());
		QTimer::singleShot(200, this, [this] { scroll_to_end_ = false; });
	});
}

void Dock::update_minimum_width()
{
	// 折り返せない部品 (候補のボタンや承認のボタンの並び) の幅に、縦のスクロールバーと余白を足す
	const QMargins margins = layout()->contentsMargins();
	const int history_width = history_layout_->minimumSize().width() +
				  scroll_->verticalScrollBar()->sizeHint().width() + 2 * scroll_->frameWidth();
	const int needed = std::max({kDockMinimumWidth, history_width + margins.left() + margins.right(),
				     layout()->minimumSize().width()});
	if (minimumWidth() != needed)
		setMinimumWidth(needed);
}

QWidget *Dock::build_empty_state()
{
	// できることとできないことを最初から見せ、悩みを一度で言葉にできなくても、選ぶだけで始められるようにする
	auto *box = new QWidget(history_);
	auto *layout = new QVBoxLayout(box);
	layout->addWidget(wrapped_label(text("Dock.Welcome"), box));
	layout->addWidget(build_help(box));
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
		return build_notice(entry);
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
	// 見出しの文は折り返す。ボタンの文言にすると折り返せず、狭いドックで会話の欄を横に押し広げる
	auto *header = new QHBoxLayout();
	auto *toggle = new QToolButton(box);
	toggle->setCheckable(true);
	toggle->setArrowType(Qt::RightArrow);
	// 道具の内部の名前ではなく、data/locale の Tool.<名前> の表示名を見せる
	const std::string tool_key = "Tool." + entry.tool_name;
	auto *caption = wrapped_label(QStringLiteral("<a href=\"toggle\">%1</a>")
					      .arg(text("Dock.SentData").arg(text(tool_key.c_str())).toHtmlEscaped()),
				      box);
	caption->setTextFormat(Qt::RichText);
	connect(caption, &QLabel::linkActivated, toggle, [toggle] { toggle->toggle(); });
	header->addWidget(toggle, 0, Qt::AlignTop);
	header->addWidget(caption, 1);
	auto *content = new QPlainTextEdit(box);
	content->setReadOnly(true);
	content->setPlainText(utf8(entry.text));
	content->setMaximumHeight(200);
	content->hide();
	connect(toggle, &QToolButton::toggled, box, [toggle, content](bool on) {
		content->setVisible(on);
		toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});
	layout->addLayout(header);
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

	// 提案は AI アシスタントの発言の一部として、話し手の見出しと同じ形で見せる
	auto *title = new QLabel(text("Proposal.Title"), frame);
	title->setStyleSheet("font-weight: bold;");
	layout->addWidget(title);
	if (!proposal->reason.empty())
		layout->addWidget(wrapped_label(utf8(proposal->reason), frame));
	const bool pending = proposal->state == ProposalState::Pending;
	// モデルの指示文に頼らず、配信中・録画中の音声の変更では必ず注意を出す
	if (proposal->live_audio && pending) {
		auto *warning = wrapped_label(text("Proposal.LiveAudio"), frame);
		warning->setStyleSheet("color: #d9534f;");
		layout->addWidget(warning);
	}

	// 変える前と後の値の差分を見せる。追加なら追加する内容を、削除なら削除するものの名前と種類を見せる
	bool removes = false;
	for (const auto &item : proposal->items) {
		QString line;
		switch (item.kind) {
		case ProposalItemKind::Change:
			line = text("Proposal.ChangeLine").arg(utf8(item.label), utf8(item.before), utf8(item.after));
			break;
		case ProposalItemKind::Add:
			line = text("Proposal.AddLine").arg(utf8(item.label), utf8(item.after));
			break;
		case ProposalItemKind::Remove:
			line = text("Proposal.RemoveLine").arg(utf8(item.label));
			removes = true;
			break;
		}
		auto *label = wrapped_label(line, frame);
		label->setTextFormat(Qt::PlainText);
		layout->addWidget(label);
	}
	if (removes && pending) {
		auto *warning = wrapped_label(text("Proposal.RemoveIrreversible"), frame);
		warning->setStyleSheet("color: #d9534f;");
		layout->addWidget(warning);
	}
	// 出力の設定は適用してもすぐには反映されないものが多いため、反映の時期を示す
	if (pending) {
		for (const ApplyTiming timing : notable_timings(*proposal))
			layout->addWidget(wrapped_label(text(apply_timing_notice_key(timing)), frame));
	}

	QString state = proposal_state_text(proposal->state);
	if (proposal->state == ProposalState::Failed && !proposal->error.empty())
		state += QStringLiteral(": ") + utf8(proposal->error);
	layout->addWidget(new QLabel(state, frame));

	auto *buttons = new QHBoxLayout();
	buttons->addStretch(1);
	const SessionState session_state = session_->state();
	if (pending && session_state == SessionState::AwaitingApproval) {
		auto *reject = new QPushButton(text("Proposal.Reject"), frame);
		auto *approve = new QPushButton(text("Proposal.Approve"), frame);
		connect(reject, &QPushButton::clicked, this, [this, proposal_id] { session_->reject(proposal_id); });
		connect(approve, &QPushButton::clicked, this, [this, proposal_id] { session_->approve(proposal_id); });
		buttons->addWidget(reject);
		buttons->addWidget(approve);
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
	// 誤りの知らせは見落とさないよう警告の色で出す。palette(mid) は暗いテーマでほとんど読めない
	if (entry.text.rfind("Error.", 0) == 0)
		label->setStyleSheet("color: #d9534f;");
	return label;
}

void Dock::show_help_window()
{
	// 配信の操作を妨げないよう非モーダルで開く。閉じたら破棄する
	auto *dialog = new QDialog(window());
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(text("Dock.Help"));
	auto *scroll = new QScrollArea(dialog);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setWidget(build_help(scroll));
	// 標準のボタンの文言は Qt の翻訳に左右されるため、プラグインの文言を使う
	auto *buttons = new QDialogButtonBox(dialog);
	buttons->addButton(text("Dock.Close"), QDialogButtonBox::RejectRole);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	auto *layout = new QVBoxLayout(dialog);
	layout->addWidget(scroll, 1);
	layout->addWidget(buttons);
	dialog->resize(420, 560);
	dialog->show();
}

QWidget *Dock::build_help(QWidget *parent)
{
	// できることと、できないことを分けて見せる
	auto *frame = card(parent, "aiaHelp");
	auto *layout = new QVBoxLayout(frame);
	auto *label = wrapped_label(text("Help.Body"), frame);
	label->setTextFormat(Qt::MarkdownText);
	layout->addWidget(label);
	return frame;
}

} // namespace aia
