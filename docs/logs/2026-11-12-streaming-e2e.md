# ストリーミングの実機での確認

## 目的

回答をストリーミングで受け取り、届いた分から表示することを、実際の OBS で確かめます。あわせて、依頼者の確認で失敗した長い一覧の依頼が、最後まで表示されることを確かめます。

## 検証環境

インストール済みの OBS 33.0.1 をコピーしたポータブル版に、ビルドしたプラグインを入れました。依頼者のシーンコレクション (20 シーン) を写し、配信のキーを含むプロファイルは写していません。モデルは gpt-6.1-terra、推論の強さは low です。手順は `tools/obs-e2e/` のスクリプトです。

## 検証方法

UI Automation でドックの入力欄に文を入れて送信を押し、0.5 秒ごとにドックの文を読み取りました。状態の文と、新しく出た文のうち最も長いものの文字数を記録しています。

## 検証結果

### すべてのシーンのすべてのソースの一覧

依頼者が 15:49 に送って失敗した文と同じ文を送りました。

```
   0.6s busy=True status='考えています..' longest=8
   1.1s busy=True status='考えています...' longest=9
   1.6s busy=True status='考えています.' longest=7
   2.1s busy=True status='考えています..' longest=8
   2.6s busy=True status='考えています.' longest=7
   3.1s busy=True status='考えています..' longest=8
   3.6s busy=True status='考えています...' longest=23
   4.1s busy=True status='考えています.' longest=23
   4.6s busy=True status='考えています...' longest=23
   5.1s busy=True status='考えています.' longest=23
   5.6s busy=True status='考えています..' longest=23
   6.2s busy=True status='考えています...' longest=23
   6.7s busy=True status='' longest=45
   7.2s busy=True status='' longest=222
   7.7s busy=True status='' longest=352
   8.4s busy=True status='' longest=506
   9.1s busy=True status='' longest=681
   9.8s busy=True status='' longest=840
  10.7s busy=True status='' longest=1026
  11.7s busy=True status='' longest=1243
  12.8s busy=True status='' longest=1482
  14.0s busy=True status='' longest=1755
  14.9s busy=True status='' longest=1986
  16.3s busy=True status='' longest=2257
  17.9s busy=True status='' longest=2634
  19.4s busy=True status='' longest=3009
  21.5s busy=True status='' longest=3444
  23.6s busy=True status='' longest=3903
  24.2s busy=True status='' longest=3951
  26.7s busy=True status='' longest=4480
  29.7s busy=True status='' longest=5156
  30.3s busy=False status='' longest=5202
```

最初の文が届くまでは考えています… の点が増え、文が届き始めると消えました。回答は 6.7 秒から 30.3 秒まで 24 段階で伸び、20 シーンの一覧が最後まで表示されました。OBS のログの記録は次のとおりです。

```
16:08:57.935: [ai-assistant-for-obs] moderations status=200 elapsed_ms=940
16:09:23.768: [ai-assistant-for-obs] responses_stream status=200 elapsed_ms=25831 response_status=completed
```

### 色ソースの追加と削除

今のシーンの後ろに真っ赤な背景を置きたい、と送り、提案を承認しました。

```
--- proposal at 7.8s
  あなた
  今のシーンの後ろに真っ赤な背景を置きたい
  シーンとソースの一覧をOpenAIに送りました
  OBSの状態と統計をOpenAIに送りました
  AIアシスタントの提案
  シーン「Empty」の最背面に真っ赤な背景を追加します。既存の「background」が覆う部分では赤が見えないことがあります。
  追加: シーン「Empty」にソース「赤い背景」(色)を追加 (色: #FF0000、高さ: 1080、幅: 1920)
  承認を待っています
  提案を承認または拒否してください
approvals=1 elapsed=11.4s
--- final new texts
  あなた
  今のシーンの後ろに真っ赤な背景を置きたい
  シーンとソースの一覧をOpenAIに送りました
  OBSの状態と統計をOpenAIに送りました
  AIアシスタントの提案
  シーン「Empty」の最背面に真っ赤な背景を追加します。既存の「background」が覆う部分では赤が見えないことがあります。
  追加: シーン「Empty」にソース「赤い背景」(色)を追加 (色: #FF0000、高さ: 1080、幅: 1920)
  適用しました
  追加の結果をOpenAIに送りました
  AIアシスタント
  シーン「Empty」の最背面に、真っ赤な「赤い背景」を追加しました。

手前の「background」が覆っている部分では、赤は見えません。
```

OBS のソースのドックでは、追加した赤い背景が既存の background より下、つまり最背面にありました。続けて、さっき追加した赤い背景を消して、と送って承認すると、ソースのドックから消えました。

```
--- proposal at 6.4s
  さっき追加した赤い背景を消して
  先ほど追加した赤い背景を削除します。削除は元に戻せません。
  削除: シーン「Empty」のソース「赤い背景」(色)
  削除は取り消せません。
  承認を待っています
  提案を承認または拒否してください
approvals=1 elapsed=8.6s
--- final new texts
  さっき追加した赤い背景を消して
  先ほど追加した赤い背景を削除します。削除は元に戻せません。
  削除: シーン「Empty」のソース「赤い背景」(色)
  削除の結果をOpenAIに送りました
  シーン「Empty」から「赤い背景」を削除しました。
```
