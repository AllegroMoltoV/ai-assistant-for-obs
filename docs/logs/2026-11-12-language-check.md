# 回答と要約の言語の確認

## 目的

AI の回答と、要点の引き継ぎの要約が、利用者の言語に合うことを確かめます。基準は OBS の表示言語で、利用者が別の言語で書いたらその言語で答える決まりです。

## 検証方法

`aia-model-eval language .tmp/.env` を実行しました。OBS の表示言語 (ja-JP、en-US) と、利用者が書く言語 (日本語、英語) の 4 通りで、通常の回答と要約をそれぞれ 1 回ずつ求めます。ひらがなかカタカナを含むかで、日本語の文かを判定します。モデルは gpt-6.1-terra、推論の強さは low です。

## 検証結果

要約の依頼文を「これまでの回答と同じ言語で書く」に直す前は、OBS が en-US で利用者が日本語の場合に、回答は日本語、要約は英語でした。そのほかの 3 通りは期待どおりでした。

直したあとは、2 回の実行とも 4 通りの回答と要約がすべて期待どおりでした。1 回目の出力を以下に載せます。

```
== locale=ja-JP user=ja reply=OK summary=OK
-- reply: この環境ではOBSの状態を直接確認できないため、原因をまだ特定できません。

- 二重の声は視聴者だけに聞こえますか？ ご自身にも聞こえますか？
- ヘッドホンを使っていますか、それともスピーカーを使っていますか？
-- summary: - 問題：配信の視聴者から、ユーザーの声が二重に聞こえると言われた。
- OBSの状態・ログ：未確認。
- 原因：未特定。音声モニタリングの設定を確認することを提案した。
- 設定変更・追加：なし。
- 次の確認：OBSの音声設定とログを調べ、声が重複する経路を特定する。
== locale=en-US user=en reply=OK summary=OK
-- reply: I can’t inspect your OBS settings from here. A doubled voice can happen when your microphone is captured twice, or when its monitored audio is picked up again.

Are you using speakers or headphones? Also, does the doubled voice have a noticeable delay, or does it sound like a slight echo?
-- summary: - Problem: Viewers say the user’s voice sounds doubled on stream.
- OBS state and logs: Not checked yet.
- Possible cause: Microphone audio monitoring was suggested as something to check, but the cause is not confirmed.
- Changes: None.
- Next: Inspect the current OBS audio sources and microphone monitoring settings.
== locale=ja-JP user=en reply=OK summary=OK
-- reply: A doubled voice often means your microphone is reaching the stream through two audio sources, or that monitoring is feeding it back into the stream.

I can’t inspect your OBS settings from here. Does your voice sound doubled in a local recording too, or only on the live stream?
-- summary: - Problem: Viewers report that the user’s voice sounds doubled on stream.
- OBS state and log: Not checked yet.
- Likely causes: Not established. Microphone audio monitoring was suggested as a starting point.
- Changes: None made or proposed.
- Open questions: Check the current OBS audio routing and log to identify the source of the duplicate voice.
== locale=en-US user=ja reply=OK summary=OK
-- reply: 声の二重音声は、スピーカーの音をマイクが拾う場合や、マイク音声が複数の経路で配信に入る場合に起こります。

こちらから現在のOBS状態を確認できないため、2点教えてください。

- 音はスピーカーとヘッドホンのどちらで聞いていますか？
- 二重音声は少し遅れて聞こえますか？それとも同時に重なって聞こえますか？
-- summary: - 問題: 配信で自分の声が二重に聞こえると視聴者に言われた。
- OBSの状態・ログ: 未確認。
- 原因: 未特定。音声モニタリングの設定を確認する方針だった。
- 変更・追加: なし。
- 未確認: 音声モニタリングの設定や、マイク音声の重複経路。
mismatches=0
```
