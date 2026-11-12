# 応答の途中の TLS の再交渉によるタイムアウト

## 背景

依頼者の実機の確認 (2026-11-12 15:49) で、すべてのシーンのすべてのソースの一覧を頼むと、OpenAI に接続できない旨の知らせが出て回答が得られませんでした。OBS のログには次の 3 件が残っていました。

```
15:49:34.904: [ai-assistant-for-obs] responses status=0 elapsed_ms=26943 transport_error="Timeout was reached"
15:50:43.401: [ai-assistant-for-obs] responses status=0 elapsed_ms=25500 transport_error="Timeout was reached"
15:51:11.145: [ai-assistant-for-obs] responses status=0 elapsed_ms=24451 transport_error="Timeout was reached"
```

プラグインの設定は、接続のタイムアウトが 10 秒、応答全体のタイムアウトが 180 秒です。失敗した時間はどちらとも合いません。同じ日のログで成功した応答は最長 8.1 秒でした。

## 検証方法

`aia-model-eval slow` で、応答に 30 秒以上かかる要求 (700 までの数を英語で書かせる) を、プラグインと同じ通信のコードと OBS 同梱の libcurl 8.12.1 で送りました。比較として、libcurl を使わない PowerShell の Invoke-RestMethod でも同じ要求を送りました。`--trace` では libcurl の状態の文だけを出しました。

## 検証結果

| 送り方 | 結果 | 時間 |
| --- | --- | --- |
| プラグインの通信のコード (接続のタイムアウト 10 秒) | Timeout was reached | 37,430 ミリ秒 |
| Invoke-RestMethod | 成功、出力 3,209 トークン | 37,097 ミリ秒 |
| libcurl、接続のタイムアウト 10 秒、状態の文を出す | Timeout was reached | 36,504 ミリ秒 |
| libcurl、接続のタイムアウト 180 秒 | 成功 | 36,299 ミリ秒 |
| libcurl、TLS 1.2 まで | 成功 | 36,919 ミリ秒 |
| 修正後のプラグインの通信のコード (接続のタイムアウト 180 秒) | 成功、出力 3,209 トークン | 36,379 ミリ秒 |

接続のタイムアウト 10 秒のときの libcurl の状態の文は次のとおりです。

```
upload completely sent off: 357 bytes
schannel: remote party requests renegotiation
schannel: renegotiating SSL/TLS connection
SSL/TLS connection timeout
schannel: renegotiation failed
trace: rc=28 (Timeout was reached) http=0 elapsed_ms=36504 bytes=0
```

接続のタイムアウト 180 秒のときは、同じ再交渉が成功しました。

```
schannel: remote party requests renegotiation
schannel: renegotiating SSL/TLS connection
schannel: SSL/TLS connection renegotiated
trace: rc=0 (No error) http=200 elapsed_ms=36299 bytes=19769
```

## 考察

OpenAI のサーバーが応答の途中で TLS の再交渉を求め、libcurl 8.12.1 の Schannel の実装が、再交渉の待ち時間に接続のタイムアウトを当てていると考えています。要求の開始から接続のタイムアウトを過ぎると、再交渉はすぐにタイムアウトになります。このため、10 秒を超える応答は必ず失敗し、成功した応答がすべて 10 秒未満だったことと合います。13:34 に起きた空欄の件も、同じ原因だと推測します。

## 対策

接続のタイムアウトを応答全体と同じ 180 秒にしました。TLS 1.2 までに抑える方法でも成功しましたが、OpenAI が TLS 1.2 の受け付けをやめるとすべての通信が失敗するため採りませんでした。
