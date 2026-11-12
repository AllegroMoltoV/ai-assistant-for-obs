#pragma once

#include <QWidget>

namespace aia {

// 会話の長さの縦の棒のゲージ。10% 刻みのブロックが下から溜まり、50%・70%・90% で色が変わる (依頼者の決定)
class LengthGauge : public QWidget {
public:
	explicit LengthGauge(QWidget *parent = nullptr);

	void set_percent(int percent);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	int percent_ = 0;
};

} // namespace aia
