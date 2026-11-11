#include "main-thread.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace aia {

bool on_main_thread()
{
	auto *app = QCoreApplication::instance();
	return app && QThread::currentThread() == app->thread();
}

void run_on_main_thread(std::function<void()> fn)
{
	auto *app = QCoreApplication::instance();
	if (!app)
		return;
	if (on_main_thread()) {
		fn();
		return;
	}
	QMetaObject::invokeMethod(app, std::move(fn), Qt::QueuedConnection);
}

void post_to_main_thread(std::function<void()> fn)
{
	if (auto *app = QCoreApplication::instance())
		QMetaObject::invokeMethod(app, std::move(fn), Qt::QueuedConnection);
}

} // namespace aia
