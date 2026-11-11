# AI Assistant for OBS Studio / OBS Studio 向け AI アシスタント

配信のトラブルを、OBS Studio のドックのチャットで相談できるプラグインです。開発中で、まだ配布していません。

## 背景

配信者は、OBS Studio で「映らない」「音が入らない」「カクつく」といったトラブルに出会っても、原因を自分で言葉にできないことが多くあります。このプラグインは、OBS の状態とログを読み、Web を調べて原因を説明し、OBS の中で直せるものは利用者の承認を得て直す相談相手を、OBS のドックとして提供します。

## 内容物

| パス | 内容 |
| --- | --- |
| `src/` | プラグイン本体。OBS とつなぐ層とドックの画面 |
| `src/core/` | libobs に依存しない層。単体テストの対象 |
| `tests/` | 単体テスト |
| `data/locale/` | 画面の文言 |
| `docs/` | 調査レポートと記録。索引は `docs/INDEX.md` |

## 使い方

利用者は自分の OpenAI の API キーを設定画面に入れて使います。OpenAI API の利用料金は利用者の OpenAI アカウントに請求されます。チャットの画面は開発中です。

## ソースからのビルド

Windows x64 で、Visual Studio 2026 と CMake 3.28 以降を使います。

```powershell
cmake --preset windows-x64 -DAIA_BUILD_TESTS=ON
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

ビルドの直後に OBS のプラグインのフォルダーへ配置する `ENABLE_POST_BUILD_INSTALL` は、既定で OFF です。導入先 `%PROGRAMDATA%\obs-studio\plugins\` はほかのプラグインと共有のため、実機で確かめるときだけ ON にします。

## License

GPL-2.0

## 更新情報

### 0.1.0 (開発中)

- リポジトリの雛形を作成しました。
