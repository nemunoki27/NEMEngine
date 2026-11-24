# Nano Everyをゲームで使う

## 最初に試す

1. Nano Everyにマイクロスイッチ（D2、押すとHIGH）、単線式超音波センサー（D4）、Chainable LED（A0がクロック、A1がデータ）、サーボ（D6）を接続します。学校の `neecLib_260629` と同じ配線です。
2. PlatformIOで `Firmware` を開き、対象のNano EveryにUploadします。書き込みにより基板上のプログラムが置き換わります。Upload後はシリアルモニターを閉じます。
3. このサンプルの `GameAssets` にある2つのC#ファイルを自分のプロジェクトの `GameAssets` 内にコピーします。シーン内のオブジェクトに `BoardExample` を追加し、プレイします。Releaseビルドでも同じスクリプトを使えます。
4. スイッチを押すとConsoleにメッセージが出ます。センサーに手を12cm以内まで近づけるとLEDが緑、サーボが90度になり、離すと黄色・10度に戻ります。

## C#から使う

`NeecBoard.Connect()` は接続開始を要求します。起動時に1台を自動選択し、基板の応答後に `IsConnected` がtrueになります。複数台の場合は `FindBoards()` の `DeviceID` で選び、`Connect(info.PortName)` を呼びます。接続の失敗理由は `Error` で確認でき、抜線後も同じメソッドで再接続できます。

| 操作 | API |
| --- | --- |
| スイッチの状態 | `Switch.IsPressed`、`Switch.WasPressedThisFrame`、`Switch.WasReleasedThisFrame` |
| 距離 | `TryGetDistanceMm(out int mm)`。未接続、測定失敗、直近300msに測定値がない場合はfalse |
| LED | `SetLedColor(byte r, byte g, byte b)`。各色0～255 |
| サーボ | `SetServoAngle(int degrees)`。10～170度 |

出力APIの戻り値は送信待ちに追加できたかを示します。スクリプト単独で所有するなら `OnDestroy` で `Dispose()` します。シーンをまたいで共有する場合は、ゲーム側のサービスで接続を1つ保持してください。Editorのプレイ停止・アセンブリ再読み込みでエンジンがポートを閉じます。

## 独自の機器や操作を追加する

`SerialDeviceInfo.Enumerate()` で列挙し、ゲームのメインスレッドで `new SerialDevice(portName, baudRate)` を呼びます。8N1で、DTR・RTSの初期値はtrueです。必要に応じてコンストラクターで指定できます。`FrameStarted` と `Received` はゲームのUpdate直前にメインスレッドで呼ばれます。`Received` の各ブロックは任意の位置で分割・結合されるため、機器の形式に合わせてフレームを復元します。`TrySend` はバイト列をキューに追加します。混雑時はfalseが返ります。

たとえば「手をかざした瞬間」はゲーム側で距離と前回の判定を比較するだけで追加できます。`BoardExample` の `wasNear` がその例です。別機種のプロトコルも `NeecBoard` と同じようにゲーム側に実装できます。

### サンプル基板の通信形式

115200bps、ASCII、1行1メッセージです。PCが `HELLO 1` を送ると、基板は `NEEC 1 0` または `NEEC 1 1` で応答します。末尾は現在のスイッチ状態です。接続後、変化時に `BUTTON 0/1`、約60msごとに `DIST ミリメートル` を送ります。測定タイムアウトは `DIST -1` です。PCから `LED R G B` または `SERVO 角度` を送り、基板が指令を適用すると `OK LED` または `OK SERVO` が返ります。出力には実際の色や軸位置を測るフィードバックはありません。

