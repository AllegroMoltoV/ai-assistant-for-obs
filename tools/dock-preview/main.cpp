// ドックの画面を OBS を起動せずに描き、画像と、文字の見切れの検査の結果を書き出す開発用の道具。
// dock.cpp をそのまま組み込み、文言は OBS と同じ libobs の text_lookup で data/locale の ini から引く。
// OpenAI には送らず、模した応答を返す送り先で、相談の会話と提案の画面を描く。
// 使い方: aia-dock-preview <出力のフォルダー> [ja-JP|en-US]
// 実行には OBS Studio の bin\64bit を PATH に入れ、QT_PLUGIN_PATH にも同じフォルダーを指定する

#include "api-key-store.h"
#include "dock.h"

#include <obs-module.h>
#include <util/text-lookup.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <string>

using nlohmann::json;

namespace {

lookup_t *g_lookup = nullptr;

QString module_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// 相談の会話の台本。エコーの相談で、状態を読み、出典つきで答えて設定の変更を提案し、承認の後に結果を答える
json message_item(const std::string &text, const json &annotations = json::array())
{
	return {{"type", "message"},
		{"role", "assistant"},
		{"content", json::array({{{"type", "output_text"}, {"text", text}, {"annotations", annotations}}})}};
}

json response(const json &output, int input_tokens)
{
	return {{"status", "completed"}, {"output", output}, {"usage", {{"input_tokens", input_tokens}}}};
}

std::deque<json> echo_script()
{
	const std::string answer =
		"マイクの「音声モニタリング」が「モニタリング有効」になっているため、配信にマイクの声が二重に入っています。"
		"OBS の公式の手引きでも、モニタリングは配信に入れない音の確認に使うよう案内されています。";
	const json citation = {{"type", "url_citation"},
			       {"url", "https://obsproject.com/kb/audio-mixer-guide"},
			       {"title", "Audio Mixer Guide"},
			       {"start_index", 60},
			       {"end_index", 70}};
	const json change_args = {{"reason", "マイクの音声モニタリングを切り、声が二重に入るのを止めます"},
				  {"changes", json::array({{{"target", "audio_volume_db"},
							    {"scene", nullptr},
							    {"source", "マイク"},
							    {"filter", nullptr},
							    {"key", nullptr},
							    {"value_json", "-6.5"}}})}};
	std::deque<json> script;
	script.push_back(response(json::array({{{"type", "function_call"},
						{"call_id", "call_1"},
						{"name", "get_obs_status"},
						{"arguments", "{}"}}}),
				  9000));
	script.push_back(response(json::array({message_item(answer, json::array({citation})),
					       {{"type", "function_call"},
						{"call_id", "call_2"},
						{"name", "change_settings"},
						{"arguments", change_args.dump()}}}),
				  14000));
	script.push_back(response(
		json::array({message_item(
			"マイクの音声モニタリングを「モニターオフ」にしました。もう一度、声が二重に聞こえるか確かめてください。")}),
		70000));
	return script;
}

class ScriptedTransport : public aia::Transport {
public:
	explicit ScriptedTransport(std::deque<json> script) : script_(std::move(script)) {}
	// 応答を返すまでの時間。読み込み中の表示を確かめるときに延ばす
	int delay_ms = 30;

	void send(aia::Endpoint endpoint, const json &request, Done done) override
	{
		json body;
		if (endpoint == aia::Endpoint::Moderations) {
			body["results"] = json::array();
			for (size_t i = 0; i < request.at("input").size(); ++i)
				body["results"].push_back({{"flagged", false}});
		} else if (!script_.empty()) {
			body = script_.front();
			script_.pop_front();
		} else {
			body = response(json::array({message_item("(台本の終わり)")}), 0);
		}
		QTimer::singleShot(delay_ms, [done, body] { done(200, body, std::string()); });
	}
	void cancel() override {}

private:
	std::deque<json> script_;
};

struct FakeTools : aia::ToolHost {
	std::string run_read_tool(const std::string &, const std::string &) override
	{
		return json({{"streaming", false}, {"recording", false}, {"fps", 60}}).dump();
	}
	aia::Parsed<aia::Proposal> prepare_change(const aia::ChangeRequest &) override
	{
		aia::Parsed<aia::Proposal> result;
		result.ok = true;
		aia::ProposalItem item;
		item.label = "マイクの音声モニタリング";
		item.before = "モニタリング有効";
		item.after = "モニターオフ";
		result.value.items.push_back(item);
		return result;
	}
	aia::Parsed<aia::Proposal> prepare_add(const aia::AddRequest &) override { return {}; }
	aia::Parsed<aia::Proposal> prepare_remove(const aia::RemoveRequest &) override { return {}; }
	bool apply(const aia::Proposal &, std::string &) override { return true; }
};

void wait_ms(int ms)
{
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
		// 作り直しで消した部品を、検査の前に実際に破棄する
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}
}

// 折り返す文字の部品のうち、文を描くのに要る高さより低く置かれたものと、
// 文言の幅より狭く置かれたボタンを、見切れとして数える
int report_clipped(QWidget *root, const QString &scene, FILE *out)
{
	int clipped = 0;
	for (QLabel *label : root->findChildren<QLabel *>()) {
		if (!label->isVisibleTo(root) || !label->wordWrap())
			continue;
		const int needed = label->heightForWidth(label->width());
		if (needed > label->height()) {
			++clipped;
			std::fprintf(out, "CLIPPED %s: label width=%d height=%d needed=%d text=%s\n",
				     qUtf8Printable(scene), label->width(), label->height(), needed,
				     qUtf8Printable(label->text().left(30).replace('\n', ' ')));
		}
	}
	for (QPushButton *button : root->findChildren<QPushButton *>()) {
		if (!button->isVisibleTo(root))
			continue;
		// スタイルシートの余白を除いた、文言を描く領域の幅と比べる
		QStyleOptionButton option;
		option.initFrom(button);
		option.text = button->text();
		const QRect contents = button->style()->subElementRect(QStyle::SE_PushButtonContents, &option, button);
		const int needed = button->fontMetrics().horizontalAdvance(button->text());
		if (needed > contents.width()) {
			++clipped;
			std::fprintf(out, "CLIPPED %s: button contents=%d needed=%d text=%s\n", qUtf8Printable(scene),
				     contents.width(), needed, qUtf8Printable(button->text()));
		}
	}
	return clipped;
}

// 会話の欄の中身の最小幅が、表示の幅に収まることを確かめる。収まらなければ横に見切れる
int report_overflow(aia::Dock &dock, const QString &scene, FILE *out)
{
	auto *scroll = dock.findChild<QScrollArea *>();
	const int content = scroll->widget()->minimumSizeHint().width();
	const int viewport = scroll->viewport()->width();
	if (content <= viewport)
		return 0;
	std::fprintf(out, "OVERFLOW %s: content minimum=%d viewport=%d\n", qUtf8Printable(scene), content, viewport);
	return 1;
}

int capture(aia::Dock &dock, const QString &dir, const QString &scene, int width, int height, FILE *out)
{
	dock.resize(width, height);
	wait_ms(400);
	const QString name = QString("%1-%2x%3").arg(scene).arg(width).arg(height);
	auto *scroll = dock.findChild<QScrollArea *>();
	// ドックは最小幅より狭くは縮まないため、実際の幅も出す
	std::fprintf(out, "%s: dock=%dx%d min_width=%d scroll=%d/%d\n", qUtf8Printable(name), dock.width(),
		     dock.height(), dock.minimumWidth(), scroll->verticalScrollBar()->value(),
		     scroll->verticalScrollBar()->maximum());
	dock.grab().save(dir + "/" + name + ".png");
	// スクロールの範囲の中身も、見えない部分を含めて画像にする
	scroll->widget()->grab().save(dir + "/" + name + "-content.png");
	return report_clipped(&dock, name, out) + report_overflow(dock, name, out);
}

bool click_button(QWidget &root, const QString &label)
{
	for (QPushButton *button : root.findChildren<QPushButton *>()) {
		if (button->text() == label && button->isVisibleTo(&root)) {
			button->click();
			return true;
		}
	}
	return false;
}

} // namespace

const char *obs_module_text(const char *val)
{
	const char *out = val;
	text_lookup_getstr(g_lookup, val, &out);
	return out;
}

// モジュールが無いため、設定のフォルダーは空になる
obs_module_t *obs_current_module(void)
{
	return nullptr;
}

extern "C" void obs_log(int, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	std::vfprintf(stderr, format, args);
	std::fputc('\n', stderr);
	va_end(args);
}

// API キーは読まず、保存済みとして描く。模した送り先はキーを使わない
namespace aia {
bool api_key_saved()
{
	return true;
}
ApiKeyStatus load_saved_api_key(std::string &)
{
	return ApiKeyStatus::NotFound;
}
ApiKeyStatus save_api_key(const std::string &)
{
	return ApiKeyStatus::Unsupported;
}
ApiKeyStatus delete_saved_api_key()
{
	return ApiKeyStatus::Unsupported;
}
} // namespace aia

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: aia-dock-preview <output-dir> [ja-JP|en-US]\n");
		return 2;
	}
	const std::string locale = argc > 2 ? argv[2] : "ja-JP";
	const std::string ini = std::string(AIA_SOURCE_DIR) + "/data/locale/" + locale + ".ini";
	g_lookup = text_lookup_create(ini.c_str());
	if (!g_lookup) {
		std::fprintf(stderr, "cannot read %s\n", ini.c_str());
		return 2;
	}

	QApplication app(argc, argv);
	const QString dir = QString::fromLocal8Bit(argv[1]);
	QDir().mkpath(dir);
	FILE *out = stdout;
	int clipped = 0;
	int failed = 0;

	// OBS の既定のテーマ (Yami、文字の大きさ 10、密度 1) のボタンと入力欄の余白と文字の大きさに近づける。
	// テーマの完全な再現ではないため、色や細かな余白は実機と違う
	app.setStyleSheet("* { font-size: 10pt; }"
			  "QPushButton { padding: 4px 16px; border: 1px solid #5a5a5a; border-radius: 4px; }"
			  "QPlainTextEdit { padding: 4px 10px; }");

	FakeTools tools;
	auto transport_owner = std::make_unique<ScriptedTransport>(echo_script());
	ScriptedTransport *transport = transport_owner.get();
	aia::Dock dock(tools, locale, std::move(transport_owner));
	dock.show();

	// 空の会話の画面。OBS の初期の配置に近い縦長と、最小幅と、広い幅
	clipped += capture(dock, dir, "1-empty", 300, 600, out);
	clipped += capture(dock, dir, "1-empty", 300, 900, out);
	clipped += capture(dock, dir, "1-empty", 450, 700, out);

	// ドックを最小幅より狭くしようとしても、会話の欄が横に見切れないこと
	clipped += capture(dock, dir, "1-empty", 200, 700, out);

	// 「?」のボタンで別の窓に出すヘルプ文。候補のボタンは会話の欄に残る
	if (!click_button(dock, "?")) {
		std::fprintf(out, "FAILED: help button not found\n");
		++failed;
	}
	wait_ms(400);
	QWidget *help = nullptr;
	for (QWidget *top : QApplication::topLevelWidgets()) {
		if (top != &dock && top->isVisible())
			help = top;
	}
	if (!help) {
		std::fprintf(out, "FAILED: help window not shown\n");
		++failed;
	} else {
		help->grab().save(dir + "/2-help-window.png");
		clipped += report_clipped(help, "2-help-window", out);
		help->close();
	}

	// 候補のボタンで相談を始める。応答を待つ間、状態の文の末尾の点が増えていくこと
	transport->delay_ms = 1500;
	if (!click_button(dock, module_text("Chip.Echo"))) {
		std::fprintf(out, "FAILED: chips disappeared after help\n");
		++failed;
	}
	wait_ms(300);
	QLabel *status = nullptr;
	for (QLabel *label : dock.findChildren<QLabel *>()) {
		if (label->isVisible() && label->text().startsWith(module_text("Dock.Thinking")))
			status = label;
	}
	if (!status) {
		std::fprintf(out, "FAILED: thinking status not shown\n");
		++failed;
	} else {
		const QString first = status->text();
		wait_ms(450);
		const QString second = status->text();
		std::fprintf(out, "busy: '%s' -> '%s'\n", qUtf8Printable(first), qUtf8Printable(second));
		if (first == second) {
			std::fprintf(out, "FAILED: thinking status does not animate\n");
			++failed;
		}
	}
	transport->delay_ms = 30;
	wait_ms(3500);
	clipped += capture(dock, dir, "3-proposal", 300, 700, out);

	// 承認して、結果の回答まで進める
	if (!click_button(dock, module_text("Proposal.Approve"))) {
		std::fprintf(out, "FAILED: approve button not found\n");
		++failed;
	}
	wait_ms(800);
	clipped += capture(dock, dir, "4-applied", 300, 700, out);

	std::fprintf(out, "clipped=%d failed=%d\n", clipped, failed);
	text_lookup_destroy(g_lookup);
	return clipped == 0 && failed == 0 ? 0 : 1;
}
