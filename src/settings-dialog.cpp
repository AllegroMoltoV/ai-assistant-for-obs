#include "settings-dialog.h"

#include "api-key-store.h"

#include <obs-module.h>

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aia {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QLabel *error_label(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setStyleSheet("color: #d9534f;");
	label->setWordWrap(true);
	label->hide();
	return label;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(text("Settings.Title"));
	setAttribute(Qt::WA_DeleteOnClose);

	auto *key_group = new QGroupBox(text("Settings.ApiKey"), this);
	status_label_ = new QLabel(key_group);
	key_edit_ = new QLineEdit(key_group);
	// 入力は伏せ字にし、保存済みのキーは表示しない
	key_edit_->setEchoMode(QLineEdit::Password);
	key_edit_->setMinimumWidth(320);
	save_button_ = new QPushButton(text("Settings.ApiKey.Save"), key_group);
	delete_button_ = new QPushButton(text("Settings.ApiKey.Delete"), key_group);
	auto *input_row = new QHBoxLayout();
	input_row->addWidget(key_edit_, 1);
	input_row->addWidget(save_button_);
	input_row->addWidget(delete_button_);
	key_message_label_ = error_label(key_group);
	auto *issue_label = new QLabel(text("Settings.ApiKey.Issue"), key_group);
	issue_label->setTextFormat(Qt::RichText);
	issue_label->setOpenExternalLinks(true);
	// プラグインは無料で、OpenAI API の利用料金は利用者の負担であることを示す
	auto *cost_label = new QLabel(text("Settings.ApiKey.Cost"), key_group);
	cost_label->setWordWrap(true);
	// API の利用が利用者のアカウントと責任で行われることと、送るデータと、無保証であることを示す
	auto *responsibility_label = new QLabel(text("Settings.ApiKey.Responsibility"), key_group);
	responsibility_label->setWordWrap(true);
	auto *key_layout = new QVBoxLayout(key_group);
	key_layout->addWidget(status_label_);
	key_layout->addLayout(input_row);
	key_layout->addWidget(key_message_label_);
	key_layout->addWidget(issue_label);
	key_layout->addWidget(cost_label);
	key_layout->addWidget(responsibility_label);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	buttons->button(QDialogButtonBox::Close)->setText(text("Settings.Close"));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(key_group);
	layout->addWidget(buttons);

	connect(save_button_, &QPushButton::clicked, this, [this] { on_save_key(); });
	connect(delete_button_, &QPushButton::clicked, this, [this] { on_delete_key(); });
	connect(key_edit_, &QLineEdit::returnPressed, this, [this] { on_save_key(); });

	refresh_status();
}

void SettingsDialog::refresh_status()
{
	if (!api_key_storage_supported()) {
		status_label_->setText(text("Settings.ApiKey.Unsupported"));
		key_edit_->setEnabled(false);
		save_button_->setEnabled(false);
		delete_button_->setEnabled(false);
		return;
	}

	std::string key;
	const ApiKeyStatus status = load_saved_api_key(key);
	if (status == ApiKeyStatus::Ok)
		status_label_->setText(text("Settings.ApiKey.Saved"));
	else if (status == ApiKeyStatus::DecryptFailed)
		status_label_->setText(text("Settings.ApiKey.DecryptFailed"));
	else
		status_label_->setText(text("Settings.ApiKey.NotSaved"));
	delete_button_->setEnabled(status == ApiKeyStatus::Ok || status == ApiKeyStatus::DecryptFailed);
}

void SettingsDialog::on_save_key()
{
	const ApiKeyStatus status = save_api_key(key_edit_->text().toStdString());
	if (status == ApiKeyStatus::InvalidKey) {
		show_key_error("Settings.ApiKey.Invalid");
		return;
	}
	if (status != ApiKeyStatus::Ok) {
		show_key_error("Settings.ApiKey.SaveFailed");
		return;
	}
	key_edit_->clear();
	key_message_label_->hide();
	refresh_status();
}

void SettingsDialog::on_delete_key()
{
	const ApiKeyStatus status = delete_saved_api_key();
	if (status != ApiKeyStatus::Ok && status != ApiKeyStatus::NotFound) {
		show_key_error("Settings.ApiKey.DeleteFailed");
		return;
	}
	key_message_label_->hide();
	refresh_status();
}

void SettingsDialog::show_key_error(const char *text_key)
{
	key_message_label_->setText(text(text_key));
	key_message_label_->show();
}

} // namespace aia
