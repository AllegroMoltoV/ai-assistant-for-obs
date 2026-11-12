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
| 会話の試験: 確定したモデルでの症状ごとの会話の流れ (依頼者の確認用) | `docs/logs/2026-11-12-conversation-review.md` |
| 会話の試験 2: あいまいな言い方と関係ない話題の会話の流れと、用語の点検 (依頼者の確認用) | `docs/logs/2026-11-12-conversation-review-2.md` |
| 会話の試験 3: 試験 2 で守られなかった決まりへの修正の確認 | `docs/logs/2026-11-12-conversation-review-3.md` |
| 古い状態で答えない決まりの確認 (利用者が途中で設定を変える場面) | `docs/logs/2026-11-12-state-change-check.md` |
| 回答と要約の言語の確認 (OBS の表示言語と利用者の言語の 4 通り) | `docs/logs/2026-11-12-language-check.md` |
| 日本語の文言の問題点 (OBS の訳との用語のずれ、文章の規約、yomiyasu の lint) | `docs/reports/2026-11-12-japanese-wording-review.md` |
| 計画の親課題 | `bd show ai-assistant-for-obs-oea` |
| 実行ログ (生の出力。git 追跡対象外) | `.logs/` |
| 実行ログ (残す価値があると判断したもの) | `docs/logs/` |
| 計画と決定事項 (Beads 使用時の正本) | `bd show <親課題>` |
| 計画 (Beads 非使用時) | `.prompts/PLANS/` |
| 判断を要する論点 (Beads 非使用時) | `.prompts/DISCUSSIONS/` |
| タスクの状態、依存関係、ブロッカー | `bd ready` / `bd blocked` / `bd list` |
