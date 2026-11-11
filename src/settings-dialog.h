#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace aia {

// 設定画面。API キーの保存と削除はボタンを押した時点で反映する
class SettingsDialog : public QDialog {
public:
	explicit SettingsDialog(QWidget *parent = nullptr);

private:
	void refresh_status();
	void on_save_key();
	void on_delete_key();
	void show_key_error(const char *text_key);

	QLabel *status_label_ = nullptr;
	QLabel *key_message_label_ = nullptr;
	QLineEdit *key_edit_ = nullptr;
	QPushButton *save_button_ = nullptr;
	QPushButton *delete_button_ = nullptr;
};

} // namespace aia
