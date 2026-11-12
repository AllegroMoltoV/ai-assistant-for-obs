#pragma once

#include <string>

namespace aia {

// モデルへの指示文。要件の「できること・できないこと」と会話の進め方 (.prompts/INIT.md の機能要件) を写したもの
const std::string &assistant_instructions();

// 回答の言語の指示。locale は OBS の表示言語 (例: ja-JP)。空なら利用者が書いた言語で答えさせる。
// 利用者の言語に合わせる (依頼者の決定、2026-11-12)。基準は OBS の表示言語で、利用者が別の言語で書いたらその言語で答える
std::string language_instruction(const std::string &locale);

} // namespace aia
