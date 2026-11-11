#pragma once

#include <string>

namespace aia {

// モデルへの指示文。要件の「できること・できないこと」と会話の進め方 (.prompts/INIT.md の機能要件) を写したもの
const std::string &assistant_instructions();

} // namespace aia
