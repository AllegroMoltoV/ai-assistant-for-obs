# 流用元 obs-realtime-translate-summary の構成の調査

調査日: 2026-11-11

## はじめに

本プラグインは、リポジトリの構成と API キーの保存・libcurl の通信・ドックと設定の画面を `D:/work/obs-realtime-translate-summary` から流用します。流用する範囲と変える箇所を決めるため、流用元の構成を読み取りだけで調べました。パスは流用元のリポジトリのルートからの相対です。

## 検証結果

### 構成とビルド

- obs-plugintemplate の仕組みのままです。`buildspec.json` 2-33 行で obs-studio 33.0.1 のソース、obs-deps 2026-10-23、qt6 2026-10-23 を sha256 つきで指定し、`.deps/` に展開します。
- `CMakeLists.txt` は 3.28...3.30 を要求します。nlohmann_json v3.12.0 を FetchContent で取得します (14-23 行) 。
- `src/core/` を静的ライブラリ `rts-core` にします (25-41 行) 。cxx_std_17 で、WIN32 では crypt32 にリンクします。
- プラグイン本体は MODULE で、`OBS::libobs`・`CURL::libcurl`・`OBS::obs-frontend-api`・`Qt6::Core/Widgets` にリンクします (43-68 行) 。Windows では OBS 同梱の libcurl.dll を実行時に使います (48-50 行) 。
- オプションは `RTS_BUILD_TESTS` (88-100 行) と `RTS_BUILD_MODEL_EVAL` (103-111 行) です。
- Windows のプリセットは `windows-x64` で、generator は Visual Studio 18 2026、binaryDir は `build_x64` です。構成は RelWithDebInfo です。
- インストール先は `%ALLUSERSPROFILE%/obs-studio/plugins` です (`cmake/windows/defaults.cmake` 10-19 行) 。`ENABLE_POST_BUILD_INSTALL` が既定で ON のため、ローカルのビルドの後に共有の導入先へ書き込みます (114-124 行) 。
- `.gitignore` は `/*` ですべてを除外し、追跡対象だけを `!` で戻す方式です。

### API キーの保存

- `src/core/api-key-file.h` は libobs に依存しません。保存先のフォルダーを引数で受け取ります。
- `src/core/api-key-file.cpp` は `CryptProtectData` で暗号化し、`api-key.bin` に保存します。説明の文字列は `L"realtime-translate-summary"` です (36 行) 。Windows 以外では `Unsupported` を返します。
- プラグイン側の `src/api-key-store.cpp` は、`obs_module_config_path("")` を保存先にします。

### OpenAI との通信

- `src/text-client.cpp` の `TextClient::create(json)` は `https://api.openai.com/v1/responses` に同期で送ります。ストリーミングは使いません。
- CONNECTTIMEOUT は 10 秒、TIMEOUT は 30 秒です (70-71 行) 。`std::atomic<bool>*` で中断できます。
- 応答からは `output[].type=="message"` の `output_text` だけを集めます (112-126 行) 。`usage` と `error` も取り出します。
- 要求は `src/core/prompts.cpp` 79-109 行で組み立てます。`store:false` を指定します。
- `curl_global_init/cleanup` は `obs_module_load/unload` で呼びます。

### ドックと設定の画面

- `OBS_FRONTEND_EVENT_FINISHED_LOADING` で `obs_frontend_add_dock_by_id` を呼んで登録します (`src/plugin-main.cpp` 199-222 行) 。登録の直後に `BasicWindow/DockState` を再適用し、新しいドックが隠れる問題を避けます。
- `src/dock.cpp` は `QWidget` を継承します。Q_OBJECT は使わず、lambda で connect します。
- `src/settings-dialog.cpp` は非モーダルの QDialog です。API キーの欄は Password の表示で、保存済みのキーは表示しません。
- UI スレッドへは `src/text-output.cpp` 25-47 行の `run_on_main_thread` と `post_to_main_thread` で戻します。`QMetaObject::invokeMethod(qApp, fn, Qt::QueuedConnection)` を使います。

### 単体テスト

- テストフレームワークは使わず、自前の `CHECK` マクロと `main` で書きます (`tests/core-tests.cpp`) 。
- API の呼び出しは抽象クラス `TextGenerator` にして、テストでは偽の実装へ差し替えます。
- 実行は `-DRTS_BUILD_TESTS=ON` で構成し、`ctest --test-dir build_x64 -C RelWithDebInfo` で行います。
- CI ではテストを実行しません。`.github` 以下に `ctest` の記述はありません。

### CI

- `push.yaml` は main とタグへの push で動きます。タグのときは draft のリリースを作ります。
- `build-project.yaml` は macOS・Ubuntu 26.04・windows-2025-vs2026 でビルドします。

### モデル比較の試験 rts-model-eval

- ソースは `tools/model-eval/main.cpp` です。`rts-model-eval <samples> <out> [effort] [all|summary] [モデル,モデル]` の形で実行します。
- キーは環境変数 `OPENAI_API_KEY` から読みます。モデルと単価は 253-257 行にハードコードされています。
- 入力は `tools/model-eval/samples/*.json`、出力は `<out>/outputs.md` と `<out>/stats.md` です。
- 実績は流用元の `docs/logs/2026-11-10-model-eval{,-low}/` と `docs/reports/2026-11-10-model-comparison.md` にあります。

### 文書と掲載

- README の章立ては、概要・背景・主な機能・必要なもの・費用・送信するデータと保存しないもの・対応環境と配布・インストール・使い方・制約・ソースからのビルド・AI 利用開示・更新情報・License・Author です。
- ロケールは `data/locale/en-US.ini` と `ja-JP.ini` です。
- `.soko-kun/` に `logo.png` と `image.gif` があります。

## 考察

- `cmake/`・`.github/`・`build-aux/`・`.clang-format`・`.gersemirc`・`.gitignore`・`CMakePresets.json` は、ほぼそのまま使えると考えています。変えるのは `buildspec.json` の名前と bundleId、ターゲット名の接頭辞、DPAPI の説明の文字列、ドックの ID だと推測します。
- `TextClient` は 30 秒のタイムアウトで、`output_text` しか読みません。道具の呼び出しと Web 検索を繰り返すチャットでは、タイムアウトを延ばし、`function_call` と出典の注釈を読む処理を足す必要があると考えています。
- 単体テストを CI で実行しない点は、本プラグインの完了条件である「Windows のビルドと単体テストが CI で合格する」と合いません。CI にテストの実行を足す必要があると考えています。
- `ENABLE_POST_BUILD_INSTALL` が既定で ON のため、フェーズ 1 のビルドでも共有の導入先へ書き込みます。フェーズ 1 では OFF で構成する必要があると考えています。

## 未確認の事項

- テンプレートのどのコミットから作ったか
- CI の Windows ジョブの依存のキャッシュの有無
