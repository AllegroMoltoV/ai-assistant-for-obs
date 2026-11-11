# 文書索引

context compaction で文脈が失われた場合は、この索引から必要な文書をたどる。

| 知りたいこと | 参照先 |
|---|---|
| プロジェクトの目的と使い方 | `README.md` |
| 要件と初回セットアップの指示 | `.prompts/INIT.md` |
| 調査レポート | `docs/reports/` |
| 流用元 obs-realtime-translate-summary の構成 | `docs/reports/2026-11-11-reference-repo-survey.md` |
| OpenAI の Responses API の仕様と料金、OBS の出力・ログ・統計・ID | `docs/reports/2026-11-11-openai-api-and-obs-behavior.md` |
| 出力の設定の項目ごとの書き込み先と反映のタイミング、書き換えてよい項目と手順を案内する項目 | `docs/reports/2026-11-11-output-settings-apply.md` |
| Web 検索と独自の関数を 1 つの要求で併用した結果 | `docs/logs/2026-11-12-web-search-function-combo.md` |
| モデルと推論の強さの比較の結果と考察 (試験の道具は `tools/model-eval/`) | `docs/reports/2026-11-12-model-comparison.md` |
| モデルの比較の会話の全文と自動の集計 | `docs/logs/2026-11-12-model-eval/` |
| 計画の親課題 | `bd show ai-assistant-for-obs-oea` |
| 実行ログ (生の出力。git 追跡対象外) | `.logs/` |
| 実行ログ (残す価値があると判断したもの) | `docs/logs/` |
| 計画と決定事項 (Beads 使用時の正本) | `bd show <親課題>` |
| 計画 (Beads 非使用時) | `.prompts/PLANS/` |
| 判断を要する論点 (Beads 非使用時) | `.prompts/DISCUSSIONS/` |
| タスクの状態、依存関係、ブロッカー | `bd ready` / `bd blocked` / `bd list` |
