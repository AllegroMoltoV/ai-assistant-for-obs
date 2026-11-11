#pragma once

#include <QWidget>

namespace aia {

// 相談のチャットのドック。チャットの画面は子課題 .5 で作る
class Dock : public QWidget {
public:
	explicit Dock(QWidget *parent = nullptr);
};

} // namespace aia
