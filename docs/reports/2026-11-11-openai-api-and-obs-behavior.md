# OpenAI の Responses API と OBS Studio の挙動の調査

調査日: 2026-11-11

## はじめに

`.prompts/INIT.md` の未確認の事項のうち、OpenAI の Responses API の Web 検索・道具の呼び出し・圧縮の仕様と料金、配信中に出力の設定を変えたときの OBS の挙動、ログと統計の取得方法、追加するフィルターとソースの ID を調べました。

## 検証方法

- OpenAI の文書は、`https://developers.openai.com/api/docs/` 配下と API リファレンスを WebFetch で取得しました。WebFetch は小型のモデルがページを要約して返すため、引用は原文の逐語とは限りません。実装で使う値は、着手時に原文で見直します。
- OBS Studio は master (fe1ed79、2026-11-10) と release/33.1 (24f396a、2026-11-03) を取得して grep しました。最新のタグは 33.0.1 で、34 系のタグとブランチはありませんでした。

## 検証結果: OpenAI

### Web 検索

- 新しい統合では `{ "type": "web_search" }` を使います (guides/tools-web-search) 。リファレンスでは `web_search` と `web_search_2025_08_26` の 2 値で、既定は `web_search` です。
- 主なパラメーターは `filters.allowed_domains`・`search_context_size`・`user_location`・`external_web_access` です。
- 料金は「Web search (incl. images): $10.00 / 1k calls + Search content tokens billed at model rates.」です (pricing) 。1 回あたり 0.01 米ドルです。取り込んだ内容が入力の単価で課金されるという明記は、gpt-4o-mini と gpt-4.1-mini の例外にしかありません。
- 出典は `message.content[].annotations` に `{"type":"url_citation","start_index","end_index","url","title"}` の形で返ります。参照した URL の全件は `include: ["web_search_call.action.sources"]` で取得できます。
- ガイドに「The web search context window is limited to 128k, even when the underlying model has a larger context window.」とあります。

### 道具の呼び出し

- 定義は `{type:"function", name, description, parameters, strict}` です。応答の `output` に `type:"function_call"` のアイテムが入り、`call_id`・`name`・JSON 文字列の `arguments` を持ちます。
- 結果は、次の要求の `input` に `{type:"function_call_output", call_id, output}` を足して返します。
- 推論モデルでは「any reasoning items returned in model responses with tool calls must also be passed back with tool call outputs」とあります。
- 「Built-in tools cannot be included in a parallel function-call batch.」とあります。
- Web 検索と独自の関数を同じ要求で使える旨は、移行ガイドの要約で確認しました。同じ `tools` 配列で併用する例は、Web 検索と道具のガイドにありませんでした。

### 圧縮

- `context_management` は `{type, compact_threshold}` の配列です。`type` は `compaction` だけで、`compact_threshold` の最小値は 1,000 です。
- 閾値を超えると、API の側で compaction の出力アイテムを出し、文脈を刈り込んでから推論を続けます。
- `POST /responses/compact` は `model` と `input` を受け取り、`output` に `{type:"compaction", id, encrypted_content}` を返します。ガイドには「opaque and not intended to be human-interpretable」とあります。
- 次の要求には、出力アイテムをそのまま `input` に渡します。最新の compaction アイテムより前のアイテムは捨ててかまいません。
- 対応モデルの一覧と料金は、compaction のガイド・conversation-state・pricing・gpt-6-luna のモデルのページで見つかりませんでした。

### モデル

| モデル | コンテキスト | 推論の強さ | 単価 (272K 以下) | 単価 (272K 超) |
| --- | --- | --- | --- | --- |
| gpt-6.1-terra | 1,050,000 | none・low・medium・high・xhigh・max | 1 / 0.05 / 5 | 2.00 / 0.10 / 7.50 |
| gpt-6-luna | 1,050,000 | 同上 | 0.10 / 0.01 / 0.50 | 0.20 / 0.02 / 0.75 |

単価は 100 万トークンあたりの米ドルで、入力 / キャッシュの入力 / 出力の順です。どちらも Responses API で Web 検索と道具の呼び出しに対応します。

### ストリーミング

`stream=true` で SSE になります。主なイベントは `response.output_text.delta`・`response.function_call_arguments.delta`・`response.output_item.done`・`response.completed`・`error` です。Web 検索のイベントの全一覧は取得していません。

### 要件の記述との照合

| 要件の記述 | 判定 |
| --- | --- |
| 道具は `{"type": "web_search"}` | 一致 |
| 検索は 1 回 0.01 米ドル | 一致 |
| 取り込んだ内容は使うモデルの入力の単価で課金 | 公式の記述は「model rates」で、入力の単価とは明記されていない |
| Web 検索のあいだは 12.8 万トークンに制限 | 一致 |
| gpt-6.1-terra の単価 1 / 0.05 / 5 | 272K 以下で一致 |
| gpt-6-luna の単価 0.10 / 0.01 / 0.50 | 272K 以下で一致 |
| 圧縮の対応モデルと料金は記載が無い | 一致 |

## 検証結果: OBS Studio

### 配信中の出力の設定の変更

- `obs_frontend_get_profile_config()` は `config_t*` を返すだけで、書き換えても自動では反映されません。反映は `OBSBasic::ResetOutputs()` で行われますが、これを呼ぶ公開の frontend API はありません。配信と録画の開始時には設定を読み直します (SimpleOutput.cpp 785-792 行) 。
- `obs_encoder_update` は、エンコーダーが稼働中なら更新を次のフレームへ遅らせます (libobs/obs-encoder.c 585-611 行) 。稼働中に変えられる項目はエンコーダーごとに違い、NVENC は CBR のビットレートだけです (plugins/obs-nvenc/nvenc.c 92-127 行) 。
- 縮小の解像度・GPU のスケーリング・FPS の約数・映像の割り当ては、稼働中は警告を出して何もしません (obs-encoder.c 935-1026 行) 。
- `obs_reset_video` は `obs_video_active()` が真なら `OBS_VIDEO_CURRENTLY_ACTIVE` (-4) を返します (libobs/obs.c 1530-1540 行) 。
- OBS の設定画面は、`obs_video_active()` が真のあいだ、映像のページ・出力のモード・エンコーダー・録画・リプレイバッファ・音声トラック・サンプルレートなどを無効にします (frontend/settings/OBSBasicSettings.cpp 1714 行・2250-2262 行・2363 行) 。

### ログ

- 現在のログを返す公開の frontend API はありません。`OBSApp::GetCurrentLog()` は内部用です (frontend/OBSApp.hpp 185-186 行) 。
- ログは Windows で `%APPDATA%\obs-studio\logs\<日時>.txt` に書かれ、行ごとに flush されます。ポータブルのモードでは置き場所が変わります (OBSApp.cpp 1783-1799 行) 。
- `base_set_log_handler` と `base_get_log_handler` はエクスポートされていますが、ロックが無く、置き換えを誤るとログファイルへの出力が止まります (libobs/util/base.c 76-83 行) 。

### 統計

| 内容 | API |
| --- | --- |
| ネットワークで落ちたフレーム | `obs_output_get_frames_dropped`・`obs_output_get_total_frames`・`obs_output_get_congestion` |
| 描画の遅れ | `obs_get_lagged_frames`・`obs_get_total_frames` |
| エンコードの遅れ | `video_output_get_skipped_frames(obs_get_video())` |
| FPS とフレームの時間 | `obs_get_active_fps`・`obs_get_average_frame_time_ns` |
| CPU とメモリ | `os_cpu_usage_info_*`・`os_get_proc_memory_usage` |

統計の画面の実装は frontend/widgets/OBSBasicStats.cpp 264-405 行です。

### 追加するフィルターとソースの ID

| 種類 | `obs_source_create` に渡す ID | 根拠 |
| --- | --- | --- |
| ノイズ抑制 | `noise_suppress_filter_v2` | ソースの `.id` は `noise_suppress_filter` で `.version = 2` 。libobs が `%s_v%d` で版つきの ID を作る (libobs/obs-module.c 1121-1128 行) |
| ゲイン | `gain_filter` | plugins/obs-filters/gain-filter.c 85 行 |
| コンプレッサー | `compressor_filter` | compressor-filter.c 503 行 |
| クロップ | `crop_filter` | crop-filter.c 286 行 |
| アプリケーション音声キャプチャ | `wasapi_process_output_capture` | plugins/win-wasapi/win-wasapi.cpp 1635 行。Windows のビルド 19041 以上で登録 |
| ブラウザソース | `browser_source` | obs-browser の obs-browser-plugin.cpp |

## 考察

- 公式の圧縮が返す要約は暗号化されており、利用者に見せられません。要件の「要約を確かめるメッセージを出す」と、U5 の推奨の「量が閾値を超えたときに API の側で圧縮する」は、そのままでは両立しないと考えています。また、API の側の自動の圧縮は利用者に勧めずに圧縮するため、手本の「約 80% で圧縮を勧める」とも流れが違うと考えています。
- 出力の設定を書き換えても稼働中は反映されないため、配信中・録画中に変えないという決定は OBS の挙動とも合うと考えています。稼働中の判定は、OBS の設定画面と同じ `obs_video_active()` を使うのが、新しい仕組みを足さない方法だと考えています。この判定では仮想カメラとリプレイバッファも稼働中に含まれると推測します。
- ログは、公開の API が無く、ログのハンドラーの差し替えには危険があるため、ログファイルの末尾を読む方法が安全だと考えています。
- `store:false` で会話を手元に持つ場合、推論のアイテムも道具の結果とともに返す必要があります。会話の状態の持ち方はこれを前提に設計する必要があると考えています。

## 未確認の事項

- Web 検索と独自の関数を同じ `tools` 配列で使ったときの実際の挙動
- 圧縮の対応モデルと料金
- Web 検索のストリーミングのイベントの全一覧
- macOS の VideoToolbox と AV1 のエンコーダーの稼働中の更新
