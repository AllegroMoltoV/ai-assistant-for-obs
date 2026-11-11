# AI Assistant for OBS Studio / OBS Studio 向け AI アシスタント

配信のトラブルを、OBS Studio のドックのチャットで相談できるプラグインです。AI が OBS の状態とログを読み、Web を調べて原因を説明します。OBS の中で直せるものは、利用者の承認を得てその場で直します。

開発中で、まだ配布していません。

> このプラグインは無料です。ただし、OpenAI API の利用料金は、利用者ご自身の OpenAI アカウントに別途請求されます。利用には、ご自身で発行した OpenAI の API キーが必要です。

## 背景

配信者は、OBS Studio で「映らない」「音が入らない」「カクつく」といったトラブルに出会っても、原因を自分で言葉にできないことが多くあります。このプラグインは、うまく言葉にできなくても、会話を通じて原因にたどり着けるようにするための相談相手です。会話の画面と操作の流れは、開発者向けの AI アシスタントのチャットの、公開された仕組みをなぞっています。

## 主な機能

- 空の会話に置いた候補から選ぶだけで相談を始められます。候補は、映らない、ゲームの音が入らない、マイクが入らない、音が二重になる、カクつく、配信が切れる、ノイズの 7 つです
- AI は、OBS の状態と統計、シーンとソース、ソースの詳細、OBS のログの末尾 300 行を読み、足りない点を聞き返しながら原因を絞り込みます
- 既知の問題を Web で調べたときは、回答に出典のリンクを付けます
- OBS の中の設定の変更と、決めた種類の追加を提案します。提案は変える前と後の値を見せ、承認したときだけ適用します。適用した提案は元に戻せます
- 「できること」のボタンか `/help` で、できることとできないことを確かめられます
- 会話が長くなると、要約して続けることを勧めます。`/compact` で手動でも要約でき、`/clear` で新しい会話にできます。要約しても画面の履歴は残ります
- AI に送った内容は、会話の中で開いて確かめられます
- 日本語と English の表示に対応しています

### できることとできないこと

| 区分 | 内容 |
| --- | --- |
| できること | 原因の調査と説明、Web の調査、OBS の中の設定の変更、ノイズ抑制・ゲイン・コンプレッサー・クロップのフィルターとアプリケーション音声キャプチャ・ブラウザソースの追加、OBS の外の直し方の案内 |
| できないこと | シーン・ソース・フィルターの削除、配信・録画の開始と停止、OBS の外の操作、会話の保存 |

削除を頼まれたときは、代わりに非表示・無効・ミュートを提案します。出力と映像の設定は、配信・録画・リプレイバッファー・仮想カメラのどれかが動いている間は変えません。エンコーダーの種類のように、プラグインから反映できない出力の設定は、OBS の設定画面での手順を案内します。

## 必要なもの

- Windows x64 の PC と OBS Studio 33 以降
- OpenAI の API キー。[OpenAI の API キーのページ](https://platform.openai.com/api-keys) で発行してください。API の利用料金の支払いの設定も必要です

## 費用

OpenAI API の利用料金は、利用者の OpenAI アカウントに請求されます。プラグインの作者に料金は発生しません。

1 回の相談のおおよその料金は、使うモデルを決めた後に、公式の単価と試験の実測から計算して記載します。

## 送信するデータと保存しないもの

- プラグインが通信する相手は `api.openai.com` だけです。利用者が送信を押すまで通信しません
- 相談に答えるため、利用者の発言と、AI が読んだ OBS の状態・設定・ログを送ります。配信キー、配信の URL に含まれるキー、URL の問い合わせ文字列の値、パスワードやトークンのような秘密の値は、送る前に伏せます
- 要求には、OpenAI 側に応答を保存させない指定の `store: false` を付けています
- 会話の履歴をファイルに保存しません。OBS のログとプラグインのログにも、会話の本文と API キーを書きません
- API キーは、Windows の DPAPI で暗号化して `%APPDATA%\obs-studio\plugin_config\ai-assistant-for-obs\` に保存します

## 対応環境と配布

| OS | 配布の方法 | 確認の範囲 |
| --- | --- | --- |
| Windows x64 | ビルド済みの ZIP | 開発中 |
| macOS、Linux | 配布しない | GitHub Actions でビルドが通ることだけを確かめます。API キーの保存は Windows だけに対応しているため、OBS の中では使えません |

## 内容物

| パス | 内容 |
| --- | --- |
| `src/` | プラグイン本体。OBS とつなぐ層、通信、ドックの画面 |
| `src/core/` | libobs に依存しない層。道具の定義、秘密の値を伏せる処理、承認と取り消し、会話の制御。単体テストの対象 |
| `tests/` | 単体テスト |
| `data/locale/` | 画面の文言 |
| `docs/` | 調査レポートと記録。索引は `docs/INDEX.md` |

## ソースからのビルド

依存ライブラリは [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) の仕組みで自動的に取得します。Windows では Visual Studio 2026 と CMake 3.28 以降が必要です。

```powershell
cmake --preset windows-x64 -DAIA_BUILD_TESTS=ON
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

ビルドの直後に OBS のプラグインのフォルダーへ配置する `ENABLE_POST_BUILD_INSTALL` は、既定で OFF です。導入先の `%PROGRAMDATA%\obs-studio\plugins\` はほかのプラグインと共有のため、実機で確かめるときだけ ON にします。

### 依存ライブラリ

- [nlohmann/json](https://github.com/nlohmann/json) v3.12.0 (MIT License)。ライセンス文は `data/licenses/` にあります
- libcurl。OBS Studio に同梱されたものを使います

## AI利用開示 / AI Disclosure

### 日本語

このプラグインの制作では、Anthropic Claude Code を、要件の調査、ソースコード、ビルドの設定、テスト、文書を含むほぼすべての工程で使用しています。ロゴは、Claude Code が SVG で作った 3 つの案から作者が選んだものです。原本は [logo.svg](docs/images/logo.svg) です。

### English

Anthropic Claude Code is used for almost the entire development process, including requirements research, source code, build configuration, tests, and documentation. The logo was chosen by the author from three SVG proposals created by Claude Code. The [SVG source](docs/images/logo.svg) is included in the repository.

## 更新情報

### 0.1.0 (開発中)

- リポジトリの雛形、道具の定義、秘密の値を伏せる処理、承認と取り消し、会話の制御、OBS とつなぐ層、ドックの画面を作成しました

---

## License

GPL-2.0 (same as OBS Studio)

## Author

AllegroMoltoV
