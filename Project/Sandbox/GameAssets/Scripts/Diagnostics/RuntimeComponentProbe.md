# Play中のComponent追加・削除の確認

Edit中に空のEntityへ `RuntimeComponentProbe` を付け、Playを開始します。
確認対象の `RuntimeComponentProbe.Target` はPlay中に生成されます。
Play前の自動保存が有効なら、Edit中に追加したEntityとProbeはSceneへ保存されます。

Game Viewへ入力フォーカスを移してキーを押し、Hierarchyの対象を選んでInspectorを確認します。

| キー | 操作 | 確認内容 |
| --- | --- | --- |
| F6 | AudioSourceを追加 | Volumeが0.25で表示される |
| F7 | AudioSourceを削除 | 次の安全地点でInspectorから消える |
| F8 | RuntimeLifecycleProbeを追加 | Awake→OnEnable→追加ログ→Startの順で出る。testValueは17 |
| F9 | RuntimeLifecycleProbeを削除 | OnDisable→OnDestroyのログが出てInspectorから消える |
| F10 | 現在の状態をログ出力 | 削除した旧参照は無効。再追加しても旧参照は無効のまま |

F6→F10→F7→F10→F6→F10、F8→F9→F8の順でも確認できます。
追加したScriptのtestValueをPlay中のInspectorで編集すると実行中の値だけが変わります。
Stopすると確認用EntityとPlay中の変更が破棄されます。
確認後はEdit中に追加したEntityとProbeを削除し、Sceneを保存してください。
