#pragma once

#include "change-request.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace aia {

// 承認の画面に出す項目名と値を、OBS の画面上の名前で作る。libobs に依存しない部分だけを置く

// 訳のキーから文言を引く関数
using TextLookup = std::function<std::string(const char *key)>;

struct UiText {
	// OBS 本体の訳。キーは frontend/data/locale の名前 (例: "Basic.Settings.Output.VideoBitrate")
	TextLookup obs;
	// プラグインの data/locale の訳
	TextLookup plugin;
};

// 出力と映像の設定の項目名。設定画面の見出しから項目までを " > " でつなぐ。例: "配信 > 映像ビットレート"。
// 出力の設定のキーは「出力」、映像の設定のキーは「映像」のタブの中の位置を返す。表に無いキーは空を返す
std::string output_setting_label(const std::string &key, const UiText &ui);

// 出力と映像の設定の値。config の文字列を設定画面の選択肢の名前に直し、単位を添える。例: "4500 kbps"。
// value は config の文字列か、未設定を表す null
std::string output_setting_value(const std::string &key, const nlohmann::json &value, const UiText &ui);

// 詳細出力のエンコーダー設定の項目名。description はエンコーダーのプロパティの説明。例: "配信 > エンコーダ設定 > ビットレート"
std::string encoder_setting_label(bool recording, const std::string &description, const UiText &ui);

// 音声の変更の項目名に使う OBS の訳のキー。音声ミキサーと詳細オーディオプロパティの列の名前
const char *audio_property_key(ChangeTarget target);

// 音声の変更の値。ミュートは「ミュート」と「ミュート解除」、モニタリングは詳細オーディオプロパティの選択肢の名前で表す
std::string audio_value_text(ChangeTarget target, const nlohmann::json &value, const UiText &ui);

// 真偽値を、真なら true_key、偽なら false_key の訳で表す。真偽値でなければ plain_value_text と同じ
std::string bool_value_text(const nlohmann::json &value, const TextLookup &lookup, const char *true_key,
			    const char *false_key, const UiText &ui);

// 名前に直せない値の表し方。null は "-"、真偽値は「はい」と「いいえ」、配列とオブジェクトは要素を区切って並べる
std::string plain_value_text(const nlohmann::json &value, const UiText &ui);

// 数の値に単位を添える。value は数か、数を表す文字列。suffix は OBS のプロパティの接尾辞のように、区切りの空白を含めて渡す
std::string number_text(const nlohmann::json &value, const std::string &suffix);

// 基本の出力モードの映像エンコーダの選択肢。OBS の設定画面と同じ並びと訳のキー
// (frontend/settings/OBSBasicSettings_Stream.cpp 1748-1800 行)。encoder_id が登録されているときだけ画面に出る
struct SimpleEncoderChoice {
	const char *value;
	const char *locale_key;
	const char *encoder_id;
};
const std::vector<SimpleEncoderChoice> &simple_encoder_choices();

} // namespace aia
