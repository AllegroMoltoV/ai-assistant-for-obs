#pragma once

#include "core/change-request.h"
#include "core/proposals.h"

#include <string>

namespace aia {

// OBS とつなぐ層。道具の実行と、提案の適用と取り消しを行う。
// すべて Qt のメインスレッドから呼ぶ。返す文字列は秘密の値を伏せ、量の上限で切ったもので、そのまま AI に送る

// CPU 使用率は前回の問い合わせからの平均のため、プラグインの読み込み時に計測を始め、解放時に止める
void start_cpu_usage_monitor();
void stop_cpu_usage_monitor();

// 取得の道具。結果は JSON の文字列 (read_obs_log だけはログの本文)
std::string read_obs_status();
std::string read_scenes_and_sources();
std::string read_source_details(const std::string &source_name);
// 現在のログファイルの末尾 300 行
std::string read_obs_log();

// 出力の稼働状態。obs_frontend_*_active() と obs_video_active() から作る
OutputActivity current_output_activity();

// OBS の設定画面が開いているとき真。開いている間は出力の設定を書き込まない
bool settings_dialog_open();

// 提案を組み立てる。現在の値を読んで items の before と after を埋め、apply_data に適用のデータを入れる。
// 対象が見つからない、稼働中で変えられない、手順の案内に回す項目を含む、のいずれかなら ok を偽にし、
// error にモデルへ返す説明を入れる
Parsed<Proposal> prepare_change_proposal(const ChangeRequest &request);
Parsed<Proposal> prepare_add_proposal(const AddRequest &request);

// 承認された提案を適用する。失敗したら error に理由を入れて偽を返す。途中まで適用した分は元に戻す
bool apply_proposal(const Proposal &proposal, std::string &error);

// 適用した提案を取り消す。変更は before の値に戻し、追加は追加したものを取り除く。
// 追加の取り消しは利用者が押したときだけ行う例外で、エージェントの道具からは呼ばない
bool undo_proposal(const Proposal &proposal, std::string &error);

} // namespace aia
