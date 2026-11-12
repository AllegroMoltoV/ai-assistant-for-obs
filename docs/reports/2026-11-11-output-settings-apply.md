# OBS Studio 33.0.1 の出力の設定の書き込み先と反映のタイミング

## はじめに

OBS のドックで動作する AI アシスタントのプラグインが、承認を得て出力の設定を書き換える機能を持つ予定です。プラグインが書き換えられるのは `obs_frontend_get_profile_config()` が返す config_t と、プロファイルのフォルダーにある `streamEncoder.json`・`recordEncoder.json` です。出力ハンドラーを作り直す `OBSBasic::ResetOutputs()` は非公開で、公開 API で使えるのは `obs_frontend_reset_video()` だけです。

依頼者の決定により、公開の手段で反映できない項目は書き換えず、OBS の設定画面での手順を案内します。本レポートでは、その線引きのために、出力の設定の項目ごとに書き込み先、読まれる場所、反映のタイミング、配信中・録画中の扱いを整理します。

## 検証方法

- 対象は `D:/work/obs-realtime-translate-summary/.deps/obs-studio-33.0.1/` のソースです。以下のパスはこのフォルダーからの相対パスで書きます。ビルドと実機での確認は行っていません。
- 通読したファイル
  - `frontend/utility/SimpleOutput.cpp` 全体
  - `frontend/utility/AdvancedOutput.cpp` 全体
  - `frontend/widgets/OBSBasic_OutputHandler.cpp` 全体
  - `frontend/utility/BasicOutputHandler.cpp` 224-313 行、`frontend/utility/BasicOutputHandler.hpp` 90-100 行
  - `frontend/OBSStudioAPI.cpp` 193-205 行、426-429 行、617-620 行
  - `frontend/widgets/OBSBasic.cpp` 1374-1524 行、1661-1732 行
  - `frontend/settings/OBSBasicSettings.cpp` 118-121 行、1927-1954 行、3003-3029 行、3233-3261 行、3384-3604 行、3755-4016 行、4270-4455 行
  - `frontend/settings/OBSBasicSettings.hpp` 150-172 行
  - `frontend/widgets/OBSBasic_MainControls.cpp` 198-221 行
  - `frontend/widgets/OBSBasic_Profiles.cpp` 254-269 行、668-769 行
  - `libobs/obs-encoder.c` 430-451 行、585-611 行、924-949 行、1183-1211 行
  - `libobs/obs.c` 1526-1544 行
- 検索した範囲と条件
  - `frontend` 配下で `ResetOutputs()`、`outputHandler->Update(`、`ResetVideo()` の呼び出し元
  - `frontend/utility` 配下で `SetupOutputs` と `Create(Simple|Advanced)OutputHandler` の呼び出し元
  - `frontend/api/obs-frontend-api.h` で `reset_video`、`get_profile_config`、イベント列挙の 20-69 行
  - `frontend/settings/OBSBasicSettings.cpp` で `WidgetChanged` と `setProperty("changed"`
- `frontend/utility/audio-encoders.cpp` は 327-345 行だけを読みました。ビットレートとエンコーダー ID の対応表の中身は読んでいません。

表で使う反映のタイミングの区分は次のとおりです。

| 区分 | 意味 |
| --- | --- |
| 次の開始時 | 次に配信・録画を開始するときに設定を読み直す |
| 次の開始時、全停止のとき | 次の開始時に読み直すのは、配信・録画・リプレイバッファー・仮想カメラがすべて止まっている場合だけ |
| reset_video ですぐ | `obs_frontend_reset_video()` で反映する。出力がすべて止まっている場合だけ |
| 作り直しが要る | 出力ハンドラーのコンストラクターでしか読まず、`ResetOutputs()` による作り直しが要る。公開の手段では反映できない |

「全停止のとき」の条件は、`SetupOutputs()` の呼び出しが `if (!Active())` で守られていることによります。`Active()` は `streamingActive || recordingActive || delayActive || replayBufferActive || virtualCamActive || multitrackVideoActive` です。根拠は `frontend/utility/BasicOutputHandler.hpp` 96-100 行、`SimpleOutput.cpp` 615-617 行と 795-797 行、`AdvancedOutput.cpp` 632-634 行、808-810 行、886-888 行、`BasicOutputHandler.cpp` 275-277 行です。

## 検証結果

### 共通の前提

| 事実 | 根拠 |
| --- | --- |
| `ResetOutputs()` は出力ハンドラーが非稼働なら作り直し、稼働中なら `outputHandler->Update()` だけを呼ぶ | `frontend/widgets/OBSBasic_OutputHandler.cpp` 33-48 行 |
| `ResetOutputs()` の呼び出し元は設定画面の保存、配信の設定の保存、自動構成ウィザード、プロファイルの切り替え、起動時、出力の解像度をソースに合わせる操作に限られる | `OBSBasicSettings.cpp` 3603 行、`OBSBasicSettings_Stream.cpp` 377 行、`wizards/AutoConfig.cpp` 386 行、`OBSBasic_Profiles.cpp` 756 行、`OBSBasic.cpp` 951 行、`OBSBasic_OutputHandler.cpp` 92 行 |
| `outputHandler->Update()` を呼ぶのは `ResetOutputs()` の 47 行だけ | `frontend` 配下の検索結果 |
| `obs_frontend_get_profile_config()` は `main->activeConfiguration` を返す。出力ハンドラーが読む `main->Config()` と同じ config である | `frontend/OBSStudioAPI.cpp` 426-429 行 |
| `streamEncoder.json`・`recordEncoder.json` は読むたびにファイルから読み直す | `AdvancedOutput.cpp` 11-34 行の `GetDataFromJsonFile` |
| 稼働中のエンコーダーに `obs_encoder_update()` を呼ぶと、設定は保持され、反映はエンコーダーのスレッドで次の機会まで遅らせて行う | `libobs/obs-encoder.c` 590 行、600-611 行 |
| 稼働中のエンコーダーには `obs_encoder_set_scaled_size()` と `obs_encoder_set_video()` を適用できない | `libobs/obs-encoder.c` 935-941 行、1195-1201 行 |
| 配信開始は `SetupStreaming` のあと `StartStreaming`、録画開始は `StartRecording`、リプレイバッファーは `StartReplayBuffer` を呼ぶ | `OBSBasic_Streaming.cpp` 127 行と 99 行、`OBSBasic_Recording.cpp` 136 行、`OBSBasic_ReplayBuffer.cpp` 98 行 |

### 単純出力モード

config のセクションはすべて `SimpleOutput` です。断りのないキーはこのセクションに属します。ファイルの記載がない行番号は `frontend/utility/SimpleOutput.cpp` のものです。

| 項目 | 書き込み先 | 読まれる場所 | 反映のタイミング | 配信中・録画中に書き換えた場合 |
| --- | --- | --- | --- | --- |
| 映像ビットレート | `VBitrate` | `Update` 277 行。`Update` を呼ぶのは `SetupOutputs` 585 行と、録画の品質が `Stream` で配信が止まっているときの `UpdateRecording` 791-792 行 | 次の開始時、全停止のとき | 稼働中の経路はない。録画中に配信を始めても `SetupOutputs` を通らず、新しい値は読まれない |
| 配信のエンコーダーの種類 | `StreamEncoder` | コンストラクター 192 行、195 行。`Update` 282 行でもプリセットのキーを選ぶために読む | 作り直しが要る | 稼働中の経路はない |
| プリセット | エンコーダーに応じて `Preset`、`QSVPreset`、`NVENCPreset2`、`AMDPreset`、`AMDAV1Preset` | `Update` 287-326 行 | 次の開始時、全停止のとき | 稼働中の経路はない |
| レート制御 | 設定項目なし。`CBR` に固定 | `Update` 328 行 | 対象外 | 対象外 |
| キーフレームの間隔 | 設定項目なし。配信サービスの推奨設定だけを適用する | `Update` 338 行の `obs_service_apply_encoder_settings` | 対象外 | 対象外 |
| x264 のオプション | `UseAdvanced`、`x264Settings` | `Update` 279 行、281 行、331-333 行 | 次の開始時、全停止のとき | 稼働中の経路はない |
| 音声ビットレート | `ABitrate` | `GetAudioBitrate` 263 行。`Update` 278 行のほか、コンストラクター 200 行と 210 行でもエンコーダー ID の選択に使う | 次の開始時、全停止のとき。エンコーダー ID はコンストラクター 13 行の `GetSimpleAACEncoderForBitrate` で決まる | 稼働中の経路はない |
| 配信と録画の音声エンコーダー | `StreamAudioEncoder`、`RecAudioEncoder` | コンストラクター 132 行、193 行、161-184 行、199-213 行 | 作り直しが要る | 稼働中の経路はない |
| 録画の音声ビットレート | 設定項目なし。録画の品質が `Stream` 以外では 192 kbps をチャンネル数で換算した値 | `UpdateRecordingAudioSettings` 395-404 行 | 対象外 | 対象外 |
| 解像度 | `Video` の `BaseCX`、`BaseCY`、`OutputCX`、`OutputCY` | `OBSBasic::ResetVideo` 1458-1461 行。`CalcCRF` 421-422 行でも録画開始時に読む | reset_video ですぐ | `ResetVideo` は 1442-1444 行で何もせず戻る。単純出力には配信専用の縮小の設定がない |
| FPS | `Video` の `FPSType`、`FPSCommon`、`FPSInt`、`FPSNum`、`FPSDen` | `OBSBasic::GetConfigFPS` 1716-1731 行。`ResetVideo` 1451 行から呼ぶ | reset_video ですぐ | 解像度と同じ |
| 録画形式 | `RecFormat2` | コンストラクター 223 行、241-248 行で出力の種類を決める。`UpdateRecording` 776 行、`ConfigureRecording` 830 行、`SetupOutputs` 590 行でも読む | `hybrid_mp4`、`hybrid_mov`、それ以外の 3 群をまたぐ変更は作り直しが要る。同じ群の中の変更は次の録画開始時 | `UpdateRecording` は録画中とリプレイバッファー稼働中に 783-785 行で何もせず戻る |
| 録画の音声トラック | `RecTracks` | `UpdateRecording` 778 行、804-808 行、`SetupOutputs` 589 行、601-605 行、`ConfigureRecording` 839 行 | 次の録画開始時。エンコーダーへの音声の接続は 601-605 行の `SetupOutputs` だけが行い、全停止のときに限られる | 録画中は 783-785 行で読まない |
| 配信の音声トラック | 設定項目なし。トラック 1 に固定。VOD トラックだけ `VodTrackEnabled` | 固定はコンストラクター 200 行の mixer 0。`VodTrackEnabled` は `IsVodTrackEnabled` 685 行、`StartStreaming` 746 行から呼ぶ | `VodTrackEnabled` は次の配信開始時 | 稼働中の経路はない |
| 録画の品質 | `RecQuality` | コンストラクター `LoadRecordingPreset` 130 行、138-185 行で録画用エンコーダーの有無と種類を決める。`UpdateRecording` 781 行、`UpdateRecordingAudioSettings` 392 行でも読む | 作り直しが要る | 稼働中の経路はない |
| 録画のエンコーダーの種類 | `RecEncoder` | コンストラクター 131 行、134 行、156 行 | 作り直しが要る | 稼働中の経路はない |
| リプレイバッファーの有無 | `RecRB` | コンストラクター 222 行 | 作り直しが要る | 稼働中の経路はない |

単純出力モードのエンコーダーの種類について、次の事実があります。

- `Update` 282-283 行では、プリセットのキーを config の `StreamEncoder` から選び、プリセットの書き込み先を実在のエンコーダー ID から選びます。319-326 行で、選んだキーの値を実在のエンコーダーへ `preset` または `preset2` として渡します。
- 録画側では、コンストラクター 134-135 行でエンコーダーの種類と品質をメンバー変数へ保存し、`UpdateRecordingSettings` 539-579 行はそのメンバー変数で分岐します。

### 詳細出力モード

config のセクションはすべて `AdvOut` です。断りのないキーはこのセクションに属します。ファイルの記載がない行番号は `frontend/utility/AdvancedOutput.cpp` のものです。

| 項目 | 書き込み先 | 読まれる場所 | 反映のタイミング | 配信中・録画中に書き換えた場合 |
| --- | --- | --- | --- | --- |
| 配信の映像ビットレート | `streamEncoder.json` の `bitrate` | `UpdateStreamSettings` 208 行。呼び出し元は `SetupStreaming` 626-628 行、`StartRecording` 802-804 行、`StartReplayBuffer` 880-882 行 | 次の配信開始時。録画用エンコーダーを持たず録画中の場合は読まない | 録画用エンコーダーを持たずリプレイバッファーだけが稼働中に配信を始めると、626 行の条件が成り立ち、稼働中の配信用エンコーダーへ `obs_encoder_update` が届く |
| 配信のプリセット、レート制御、キーフレームの間隔 | `streamEncoder.json` の各キー。例は `preset`、`rate_control`、`keyint_sec` | 同上。209 行で既定値を補い、211-238 行で配信サービスの推奨値を適用する。`keyint_sec` は 236-238 行で小さいほうを採る | 同上 | 同上 |
| 録画の映像の設定 | `recordEncoder.json` の各キー | `UpdateRecordingSettings` 268-269 行。呼び出し元は `StartRecording` 798-801 行、`StartReplayBuffer` 876-879 行 | 次の録画開始時 | 稼働中の経路はない |
| `rate_control` によるリプレイバッファーの上限 | `streamEncoder.json` または `recordEncoder.json` の `rate_control` | コンストラクター 142-148 行で `usesBitrate` を決め、`StartReplayBuffer` 912 行で使う | この用途に限り作り直しが要る | 稼働中の経路はない |
| 配信のエンコーダーの種類 | `Encoder` | コンストラクター 63 行、129 行。`UpdateStreamSettings` 206 行では NVENC の判定にだけ使う | 作り直しが要る | 稼働中の経路はない |
| 録画のエンコーダーの種類 | `RecEncoder`。`none` は配信用エンコーダーの共用 | コンストラクター 65 行、75 行、118-126 行 | 作り直しが要る | 稼働中の経路はない |
| 音声エンコーダーの種類 | `AudioEncoder`、`RecAudioEncoder` | コンストラクター 64 行、66 行、76 行、154-192 行 | 作り直しが要る | 稼働中の経路はない |
| 音声ビットレート | `Track1Bitrate` から `Track6Bitrate` | `GetAudioBitrate` 582 行。`UpdateAudioSettings` 525 行、529 行。呼び出し元は `SetupStreaming` 630 行、`StartRecording` 806 行、`StartReplayBuffer` 884 行 | 次の開始時 | 3 か所の呼び出しは稼働状態で守られていない。配信中に録画を始めると、稼働中の配信用音声エンコーダーへ 544 行と 547 行の `obs_encoder_update` が届く。録画中に配信を始めた場合は、録画用の音声エンコーダーへ 527 行の更新が届く |
| 配信の縮小 | `RescaleRes`、`RescaleFilter` | 配信側の `SetupStreaming` 295-296 行、303-308 行、321-322 行。`SetupOutputs` 568 行から呼ぶ | 次の開始時、全停止のとき | 稼働中は libobs が `obs_encoder_set_scaled_size` を拒否する |
| 録画の縮小 | `RecRescaleRes`、`RecRescaleFilter` | `SetupRecording` 336-337 行、370-378 行。`SetupOutputs` 573 行から呼ぶ | 次の開始時、全停止のとき。録画用エンコーダーを持つ場合だけ有効 | 同上 |
| 解像度 | `Video` の `BaseCX`、`BaseCY`、`OutputCX`、`OutputCY` | `OBSBasic::ResetVideo` 1458-1461 行 | reset_video ですぐ | `ResetVideo` は 1442-1444 行で何もせず戻る |
| FPS | `Video` の `FPSType`、`FPSCommon`、`FPSInt`、`FPSNum`、`FPSDen` | `OBSBasic::GetConfigFPS` 1716-1731 行 | reset_video ですぐ | 同上 |
| 録画形式 | `RecFormat2` | コンストラクター 67 行、105-112 行で出力の種類を決める。`SetupRecording` 340-343 行で fragmented と flv を判定し、`StartRecording` 814 行で拡張子を決める | `hybrid_mp4`、`hybrid_mov`、それ以外の 3 群をまたぐ変更は作り直しが要る。同じ群の中では、拡張子は次の録画開始時、fragmented と flv の判定は次の開始時、全停止のとき | 稼働中の経路はない |
| 録画の種類 | `RecType`、`FFOutputToFile` | コンストラクター 62 行、73-74 行 | 作り直しが要る | 稼働中の経路はない |
| 録画の音声トラック | `RecTracks`。flv では `FLVTrack` | `SetupRecording` 345-349 行、385-401 行。`SetupOutputs` から呼ぶ | 次の開始時、全停止のとき | 配信中に録画を始めても読まれない |
| 配信の音声トラック | `TrackIndex` | コンストラクター 176-178 行で配信用音声エンコーダーの mixer を決める。`UpdateAudioSettings` 501 行、543-545 行ではビットレートを適用するトラックの選択に使う | 作り直しが要る | 稼働中の経路はない |
| VOD トラック | `VodTrackIndex`、`VodTrackEnabled` | `VodTrackIndex` はコンストラクター 186-187 行。`VodTrackEnabled` は `VodTrackMixerIdx` 589 行、`StartStreaming` 759 行から呼ぶ | `VodTrackIndex` は作り直しが要る。`VodTrackEnabled` は次の配信開始時 | 稼働中の経路はない |
| SRT と RIST の複数音声トラック | `StreamMultiTrackAudioMixes` | `SetupStreaming` 622 行、695-698 行。`SetupStreaming` 297 行、313-318 行 | 次の配信開始時 | 稼働中の経路はない |
| リプレイバッファーの有無 | `RecRB` | コンストラクター 88 行 | 作り直しが要る | 稼働中の経路はない |
| 配信サービスの推奨設定の適用 | `ApplyServiceSettings`、`Stream1` の `IgnoreRecommended` | `UpdateStreamSettings` 203-204 行、`UpdateAudioSettings` 499-500 行 | 次の開始時 | 音声ビットレートの行と同じ |

### 両モードのコンストラクターで読まれるほかの項目

| 書き込み先 | 読まれる場所 | 反映のタイミング |
| --- | --- | --- |
| `Output` の `Mode` | `OBSBasic::ResetOutputs` 30-37 行 | 作り直しが要る |
| `Stream1` の `EnableMultitrackVideo` | `BasicOutputHandler.cpp` 237-243 行 | 作り直しが要る |
| `Stream1` の `WHIPSimulcastTotalLayers` | `BasicOutputHandler.cpp` 245-247 行、`SimpleOutput.cpp` 82-87 行、`AdvancedOutput.cpp` 135-140 行 | 作り直しが要る。WHIP の同時配信用エンコーダーは、作成時の映像の幅と高さも保持する |

### obs_frontend_reset_video の動作

| 事実 | 根拠 |
| --- | --- |
| `obs_frontend_reset_video()` の戻り値は `void` | `frontend/api/obs-frontend-api.h` 237 行 |
| 中身は `main->ResetVideo()` の直接の呼び出しで、`QMetaObject::invokeMethod` を通さない | `frontend/OBSStudioAPI.cpp` 617-620 行 |
| `ResetVideo` は、出力ハンドラーが稼働中なら `OBS_VIDEO_CURRENTLY_ACTIVE` を返して何もしない。稼働の判定は仮想カメラとリプレイバッファーを含む `Active()` | `OBSBasic.cpp` 1442-1444 行、`BasicOutputHandler.hpp` 96-100 行 |
| `ResetVideo` は `Video` セクションから FPS、色形式、色空間、色範囲、基本解像度、出力解像度、縮小フィルターを読み、`obs_reset_video` に渡す | `OBSBasic.cpp` 1451-1467 行、1483 行 |
| 基本解像度か出力解像度が 32 未満なら、1920x1080 または基本解像度へ直して config に書き戻す | `OBSBasic.cpp` 1469-1481 行 |
| libobs 側でも `obs_video_active()` が真なら `OBS_VIDEO_CURRENTLY_ACTIVE` を返す。フロントエンドの出力ハンドラー以外の出力も判定に含まれる | `libobs/obs.c` 1531-1533 行 |
| libobs は出力解像度の幅を 4 の倍数、高さを 2 の倍数へ切り下げる。config は書き換えない | `libobs/obs.c` 1543-1544 行 |
| 成功時はプレビューの大きさを直し、`CanvasResized` と `OutputResized` を発行する。出力ハンドラーは作り直さない | `OBSBasic.cpp` 1489-1521 行 |
| 映像エンコーダーへの新しい映像の接続は、次の開始時の `SetupOutputs` で行う | `SimpleOutput.cpp` 586 行、597 行、`AdvancedOutput.cpp` 557-560 行 |
| 設定画面の保存でも、`ResetOutputs` を含む `SaveOutputSettings` のあとに `ResetVideo` を呼ぶ | `OBSBasicSettings.cpp` 3763-3765 行、3784-3786 行 |
| `obs_video_active()` は libobs の公開関数 | `libobs/obs.h` 744 行 |

### 設定画面を開いている間の書き換え

| 事実 | 根拠 |
| --- | --- |
| 設定画面はモーダルの `exec()` で開く。多重に開かないよう関数内の static 変数 `settings_already_executing` で守る | `OBSBasic_MainControls.cpp` 198-221 行 |
| 設定画面はコンストラクターで全項目を読み込む。開いている間に読み直す経路は、変更を破棄するときの `LoadSettings(true)` だけ | `OBSBasicSettings.cpp` 852 行、3003-3029 行、3858 行 |
| エンコーダーの設定の画面は、開いたときに JSON ファイルを読み込む | `OBSBasicSettings.cpp` 1927-1943 行 |
| OK と適用は `SaveSettings` を呼ぶ。出力の項目は、出力のページで何か変更したときだけ `SaveOutputSettings` で保存する。映像の項目も同様 | `OBSBasicSettings.cpp` 3992-4005 行、3763-3771 行 |
| 変更の印は、利用者がウィジェットを操作したときに `changed` プロパティへ付く。読み込み中は付かない | `OBSBasicSettings.cpp` 4289-4298 行、4448-4455 行、`OBSBasicSettings.hpp` 171 行 |
| `SaveCombo`、`SaveComboData`、`SaveCheckBox`、`SaveEdit`、`SaveSpinBox`、`SaveGroupBox` は、変更の印があるウィジェットだけを書き戻す | `OBSBasicSettings.cpp` 949-1015 行 |
| `SaveOutputSettings` には変更の印を確かめずに書き戻す項目がある。`Output/Mode`、`SimpleOutput/RecTracks`、`AdvOut/TrackIndex`、`AdvOut/StreamMultiTrackAudioMixes`、`AdvOut/RecType`、`AdvOut/RecSplitFileType`、`AdvOut/RecTracks`、`AdvOut/FLVTrack`、`AdvOut/FFOutputToFile`、`AdvOut/FFFormat` ほか FFmpeg 出力の形式とエンコーダー、`AdvOut/FFAudioMixes`、VOD トラックの欄がある場合の `AdvOut/VodTrackIndex` | `OBSBasicSettings.cpp` 3465 行、3520 行、3528-3531 行、3544-3545 行、3549-3558 行、3565 行、3568-3576 行、3593-3594 行 |
| `streamEncoder.json`・`recordEncoder.json` は、エンコーダーの設定の画面に変更の印があるときだけ、画面が持つ設定の全体でファイルを上書きする | `OBSBasicSettings.cpp` 3384-3401 行、3601-3602 行 |
| `SaveOutputSettings` の最後に `ResetOutputs` を呼ぶ | `OBSBasicSettings.cpp` 3603 行 |
| `SaveSettings` は最後に `config_save_safe(main->Config(), ...)` で config 全体をファイルへ保存する | `OBSBasicSettings.cpp` 3788 行 |
| 映像の項目の書き戻しはすべて変更の印を確かめる | `OBSBasicSettings.cpp` 3242-3260 行 |
| フロントエンド API のイベントに、設定画面の開閉を知らせるものはない | `frontend/api/obs-frontend-api.h` 20-69 行 |
| 設定画面は `QDialog` を継承した `Q_OBJECT` のクラス `OBSBasicSettings` で、objectName は `OBSBasicSettings` | `OBSBasicSettings.hpp` 40-41 行、`frontend/forms/OBSBasicSettings.ui` 4 行 |

### プロファイルの切り替え

| 事実 | 根拠 |
| --- | --- |
| 公開 API の `obs_frontend_set_current_profile()` はプロファイルのメニューの項目を発火する | `OBSStudioAPI.cpp` 193-205 行 |
| 現在のプロファイルと同じ名前を選んでも何もしない | `OBSBasic_Profiles.cpp` 262-269 行 |
| 別のプロファイルへ切り替えると、`ResetVideo` と `ResetOutputs` を呼ぶ | `OBSBasic_Profiles.cpp` 707-710 行、751-756 行 |

## 考察

### 反映の経路

公開の手段で反映できる経路は、次の開始時の読み直しと `obs_frontend_reset_video()` の 2 つだけと考えています。`Update()` が稼働中のエンコーダーに届く経路は、詳細出力の音声ビットレートと、リプレイバッファー稼働中の配信の映像の設定に限られ、どちらも別の出力の開始という副作用に依存します。この経路を配信中の変更の手段として案内するのは避けるべきと考えています。

「次の開始時、全停止のとき」の項目は、仮想カメラやリプレイバッファーが動作している間に書き換えると、それらを止めるまで反映されません。プラグインでは、書き換えの前に `obs_frontend_streaming_active()`、`obs_frontend_recording_active()`、`obs_frontend_replay_buffer_active()`、`obs_frontend_virtualcam_active()` で稼働状態を確かめ、稼働中なら「すべての出力を止めたあとの開始時に反映されます」と伝えるのがよいと考えています。

`obs_frontend_reset_video()` は戻り値が無く、稼働中は黙って何もしません。呼ぶ前に上の 4 関数と `obs_video_active()` で非稼働を確かめ、呼んだあとに `obs_get_video_info()` で実際の解像度と FPS を読み直して成否を判定する必要があると考えています。また `invokeMethod` を通さずプレビューなどの UI を触るため、Qt のメインスレッドから呼ぶ必要があると考えています。

### 書き換えると不整合を起こす項目

コンストラクターでしか読まない項目を config だけ書き換えると、反映されないだけでなく、次の開始時に食い違いを生む項目があると考えています。

- 単純出力の `StreamEncoder` を書き換えると、`Update` は新しいエンコーダー用のプリセットの値を古いエンコーダーへ渡します。x264 から NVENC へ変えた場合、NVENC 用の `NVENCPreset2` の値が x264 の `preset` に入ると考えています。
- 単純出力の `RecQuality` を `Stream` からほかの値へ書き換えると、`UpdateRecording` 801-809 行が作られていない `audioTrack[]` を録画へ接続しようとし、録画の開始に失敗すると考えています。
- 詳細出力の `Encoder` を書き換えたうえで新しいエンコーダー用の `streamEncoder.json` を書くと、古いエンコーダーの既定値に新しいエンコーダーのキーが混ざって適用されると考えています。
- 詳細出力の `TrackIndex` を書き換えると、配信の音声は古いトラックのまま、ビットレートだけが新しいトラックの値で適用されると考えています。

### 設定画面による上書き

設定画面を開いている間にプラグインが書き換えた値は、設定画面が上書きすることがあります。上書きの範囲は次のとおりです。

- 利用者が出力のページで何も変更せずに OK を押した場合は、出力の項目を書き戻さず、プラグインの値が残ります。
- 利用者が出力のページで何か 1 つでも変更した場合は、変更したウィジェットに加えて、変更の印を確かめない項目を開いた時点の値で書き戻します。`RecTracks`、`TrackIndex`、`RecType`、`StreamMultiTrackAudioMixes` などがこれに当たります。エンコーダーの設定の画面を変更した場合は、JSON ファイル全体を開いた時点の内容に変更を加えた値で上書きします。
- 映像のページの項目は、変更したウィジェットだけを書き戻します。

設定画面はモーダルで開くため、利用者は開いている間にドックの承認ボタンを押せないと考えています。ただし `exec()` の中でもイベントループは回るため、承認後に非同期で書き込む処理は設定画面の表示中にも走りうると考えています。

設定画面の表示を知る手段は OBS の公開 API にありません。Qt の `QApplication::activeModalWidget()` で得たウィジェットの objectName が `OBSBasicSettings` であることを確かめる方法なら使えると考えていますが、実機では確かめていません。書き込みの直前にこの判定を行い、表示中なら書き込みを保留するのが安全と考えています。

### 書き換えてよい項目

いずれも書き換えたあと、反映のタイミングを利用者へ伝える前提です。

| モード | 項目 | 書き込み先 | 反映のタイミング |
| --- | --- | --- | --- |
| 共通 | 基本解像度、出力解像度 | `Video/BaseCX`、`BaseCY`、`OutputCX`、`OutputCY` | 全停止のときに `obs_frontend_reset_video()` ですぐ |
| 共通 | FPS | `Video/FPSType`、`FPSCommon`、`FPSInt`、`FPSNum`、`FPSDen` | 同上 |
| 単純出力 | 映像ビットレート | `SimpleOutput/VBitrate` | 次の開始時、全停止のとき |
| 単純出力 | プリセット | 現在の `StreamEncoder` に対応する `SimpleOutput/Preset` などのキー | 次の開始時、全停止のとき |
| 単純出力 | 音声ビットレート | `SimpleOutput/ABitrate` | 次の開始時、全停止のとき |
| 単純出力 | x264 のオプション | `SimpleOutput/UseAdvanced`、`x264Settings` | 次の開始時、全停止のとき |
| 単純出力 | 録画の音声トラック | `SimpleOutput/RecTracks` | 次の開始時、全停止のとき |
| 単純出力 | 録画形式のうち、同じ群の中の変更 | `SimpleOutput/RecFormat2` | 次の録画開始時。flv が絡む変更は全停止のとき |
| 単純出力 | VOD トラックの有無 | `SimpleOutput/VodTrackEnabled` | 次の配信開始時 |
| 詳細出力 | 配信の映像ビットレート、プリセット、キーフレームの間隔、レート制御 | `streamEncoder.json` の `bitrate`、`preset`、`keyint_sec`、`rate_control` など。現在の `AdvOut/Encoder` のキーに限る | 次の配信開始時 |
| 詳細出力 | 録画の映像の設定 | `recordEncoder.json`。現在の `AdvOut/RecEncoder` のキーに限る | 次の録画開始時 |
| 詳細出力 | 音声ビットレート | `AdvOut/Track1Bitrate` から `Track6Bitrate` | 次の開始時 |
| 詳細出力 | 配信の縮小、録画の縮小 | `AdvOut/RescaleRes`、`RescaleFilter`、`RecRescaleRes`、`RecRescaleFilter` | 次の開始時、全停止のとき |
| 詳細出力 | 録画形式のうち、同じ群の中の変更 | `AdvOut/RecFormat2` | 次の録画開始時。fragmented と flv の判定は全停止のとき |
| 詳細出力 | 録画の音声トラック | `AdvOut/RecTracks`、`FLVTrack` | 次の開始時、全停止のとき |
| 詳細出力 | SRT と RIST の複数音声トラック、VOD トラックの有無 | `AdvOut/StreamMultiTrackAudioMixes`、`VodTrackEnabled` | 次の配信開始時 |

詳細出力の `rate_control` は、映像の設定としては次の開始時に反映されます。リプレイバッファーの容量の上限の扱いだけは作り直すまで古い値のままです。この差は影響が小さいため、書き換えてよい項目に含めました。

### 手順を案内する項目

| モード | 項目 | 書き込み先 |
| --- | --- | --- |
| 共通 | 出力モード | `Output/Mode` |
| 共通 | マルチトラック映像、WHIP の同時配信の層数 | `Stream1/EnableMultitrackVideo`、`WHIPSimulcastTotalLayers` |
| 単純出力 | 配信のエンコーダーの種類 | `SimpleOutput/StreamEncoder` |
| 単純出力 | 配信と録画の音声エンコーダー | `SimpleOutput/StreamAudioEncoder`、`RecAudioEncoder` |
| 単純出力 | 録画の品質 | `SimpleOutput/RecQuality` |
| 単純出力 | 録画のエンコーダーの種類 | `SimpleOutput/RecEncoder` |
| 単純出力 | 録画形式のうち、`hybrid_mp4`、`hybrid_mov`、それ以外の群をまたぐ変更 | `SimpleOutput/RecFormat2` |
| 単純出力 | リプレイバッファーの有無 | `SimpleOutput/RecRB` |
| 詳細出力 | 配信のエンコーダーの種類 | `AdvOut/Encoder` |
| 詳細出力 | 録画のエンコーダーの種類、配信用エンコーダーの共用の有無 | `AdvOut/RecEncoder` |
| 詳細出力 | 音声エンコーダーの種類 | `AdvOut/AudioEncoder`、`RecAudioEncoder` |
| 詳細出力 | 配信の音声トラック | `AdvOut/TrackIndex` |
| 詳細出力 | VOD トラックの番号 | `AdvOut/VodTrackIndex` |
| 詳細出力 | 録画の種類 | `AdvOut/RecType`、`FFOutputToFile` |
| 詳細出力 | 録画形式のうち、群をまたぐ変更 | `AdvOut/RecFormat2` |
| 詳細出力 | リプレイバッファーの有無 | `AdvOut/RecRB` |
| 両モード | エンコーダーの種類を変えたうえでの、新しいエンコーダー用のプリセットや JSON の設定 | 上記のエンコーダーの種類と同時に案内する |
| 両モード | 配信中・録画中の解像度と FPS | `Video` セクション。出力を止めてから設定画面で変更する手順を案内する |

設定画面の OK で `ResetOutputs` が走るため、案内する手順は「設定画面で変更し、すべての出力を止めた状態で OK を押す」でよいと考えています。

別のプロファイルへ切り替えて戻すと公開 API だけで作り直しを起こせますが、配信サービスの設定やホットキーまで読み直す重い操作です。利用者の意図しない副作用が大きいため、反映の手段としては採らないのがよいと考えています。

## 未確認の事項

- ビルドと実機での動作確認は行っていません。表はすべてソースの読解に基づきます。
- 稼働中に `obs_encoder_update` を受けた音声エンコーダーと映像エンコーダーで、途中のビットレートの変更の可否は各エンコーダーの `update` の実装に依存します。この実装は読んでいません。
- 単純出力の `ABitrate` で選ばれるエンコーダー ID が、ビットレートによって変わる組み合わせの有無は、`frontend/utility/audio-encoders.cpp` の対応表を読んでいないため未確認です。
- 単純出力で、配信中に `RecTracks` へ新しいトラックを足して録画を始めた場合、そのトラックの音声エンコーダーに音声が接続されず録画の開始に失敗する可能性を、実機では確かめていません。詳細出力ではこの問題は起きません。`SetupOutputs` が全トラックへ音声を接続しているためです。
- 単純出力の `RecQuality` と `StreamEncoder` を書き換えた場合の不整合は、考察に書いた読解による推定で、実機では確かめていません。
- `QApplication::activeModalWidget()` で設定画面を判定する方法は、実機では確かめていません。
- 配信サービスの推奨設定を適用する `obs_service_apply_encoder_settings` の中身は読んでいません。ビットレートやキーフレームの間隔がサービスの上限で書き換えられる範囲は未確認です。
- マルチトラック映像を有効にした配信の経路の `SetupMultitrackVideo` は読んでいません。この経路では映像エンコーダーを別に作るため、表の反映のタイミングが当てはまらない可能性があります。
