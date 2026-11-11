#pragma once

#include <functional>

namespace aia {

// OBS のソースの操作とドックの更新は Qt のメインスレッドで行う。作業のスレッドからはこれらで移す

// 呼び出し元がメインスレッドならその場で、そうでなければキューに入れて実行する
void run_on_main_thread(std::function<void()> fn);

// 呼び出し元のスレッドによらず、メインスレッドのキューに入れる
void post_to_main_thread(std::function<void()> fn);

bool on_main_thread();

} // namespace aia
