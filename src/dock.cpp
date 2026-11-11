#include "dock.h"

#include "settings-dialog.h"

#include <obs-module.h>

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace aia {

namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

Dock::Dock(QWidget *parent) : QWidget(parent)
{
	auto *placeholder = new QLabel(text("Dock.Placeholder"), this);
	placeholder->setWordWrap(true);
	auto *settings_button = new QPushButton(text("Dock.Settings"), this);
	// 設定画面は非モーダルで開き、配信の操作を妨げない
	connect(settings_button, &QPushButton::clicked, this, [this] { (new SettingsDialog(window()))->show(); });

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(placeholder);
	layout->addStretch(1);
	layout->addWidget(settings_button);
}

} // namespace aia
