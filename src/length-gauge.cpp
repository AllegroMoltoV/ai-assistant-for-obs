#include "length-gauge.h"

#include "core/conversation.h"

#include <obs-module.h>

#include <QPainter>

namespace aia {

namespace {

constexpr int kBlocks = 10;
constexpr int kBlockGap = 2;

// 段階ごとの溜まったブロックの色。明暗どちらのテーマでも見分けられる色
QColor fill_color(int tier)
{
	switch (tier) {
	case 3:
		return QColor(0xd9, 0x53, 0x4f);
	case 2:
		return QColor(0xf0, 0x8c, 0x00);
	case 1:
		return QColor(0xe6, 0xb8, 0x00);
	default:
		return QColor(0x3d, 0x9b, 0xe9);
	}
}

} // namespace

LengthGauge::LengthGauge(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(10);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	set_percent(0);
}

void LengthGauge::set_percent(int percent)
{
	percent_ = percent;
	setToolTip(QString::fromUtf8(obs_module_text("Dock.LengthGauge")).arg(percent));
	update();
}

QSize LengthGauge::sizeHint() const
{
	return QSize(12, 80);
}

void LengthGauge::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	const int filled = length_blocks(percent_);
	const int block_height = (height() - kBlockGap * (kBlocks - 1)) / kBlocks;
	const QColor empty = palette().color(QPalette::Mid);
	const QColor fill = fill_color(length_tier(percent_));
	for (int i = 0; i < kBlocks; ++i) {
		// 下のブロックから溜まる
		const int y = height() - (i + 1) * block_height - i * kBlockGap;
		painter.fillRect(QRect(0, y, width(), block_height), i < filled ? fill : empty);
	}
}

} // namespace aia
