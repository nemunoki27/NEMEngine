# Claude Code CLI 共通実装ルール

このファイルは、アップロード済みの `Engine.zip` に含まれる NEMEngine の現行コードを前提にした実装指示書です。単なる調査や提案ではなく、記載された完了条件を満たすところまで実装してください。

## 最優先事項

1. 処理速度: gameplay のフレーム更新で不要な reflection、JSON、文字列検索、ヒープ確保を行わない。
2. 拡張性: 新しい component、asset type、scene event、serialized field type を追加しやすい責務分離にする。
3. 汎用性: 特定ゲーム専用の例外処理やハードコードを避ける。
4. 安全性: C++ / C# 境界で use-after-free、ABI 不一致、例外越境、古い handle の誤参照を発生させない。
5. 完成度: TODO、仮実装、空メソッド、将来対応コメントだけを残して「完了」としない。

## 作業方法

- 実装開始前に、対象ファイルと関連コードを読んで現在の設計を把握する。
- 既存 API を変更する場合は、参照箇所をリポジトリ全体で検索し、呼び出し側も同時に更新する。
- 新しい `.h` / `.cpp` を追加した場合は `Engine/NEMEngine.vcxproj` と `Engine/NEMEngine.vcxproj.filters` を更新する。
- 新しい `.cs` を追加した場合は SDK-style project の包含状態を確認する。
- Debug / Develop / Release の差異を意識する。診断機能は Release で無制限にコストを発生させない。
- serialization migration を入れる場合は、旧 scene / prefab の読み込み互換を維持する。
- 仕様上見送る項目は勝手に実装しない。見送る理由と再開条件をコードコメントまたは報告に記載する。

## 作業完了時の報告形式

- 実装した項目
- 変更・追加ファイル一覧
- 互換性のために残した移行処理
- 実行した build / test と結果
- 実行できなかった test と理由
- 未完了項目。原則として 0 件であること。外部要因で不可能な場合のみ、具体的な blocker を記載する。


---

# 12. Editor の C# scripting 支援機能を完成させる

## 目的

C# scripting を日常的なゲーム開発で使える Editor 体験へ引き上げる。エラー、Missing Script、Undo、runtime debug、profiler を Editor 内で追えるようにする。

## 前提

- DLL reload: `04_edit_mode_dll_reload.md`
- Inspector schema: `05_inspector_serialization.md`
- Script manifest: `06_script_manifest_stable_identity.md`
- lifecycle execution order: `03_lifecycle_multi_pass.md`

## 必須機能 1: Compiler Error List

build stdout / stderr を構造化する。

保持項目:

```text
severity
error code
message
file path
line
column
build id
timestamp
raw line
```

UI:

- error / warning filter
- build ごとの clear / history
- 件数表示
- raw log へのリンク
- file、line、column へジャンプ

標準 MSBuild 出力 parse を実装し、parse 不能行は raw console に残す。

## 必須機能 2: IDE jump

Editor 設定へ IDE 起動 command を持たせる。

例:

```text
Visual Studio
VS Code: code -g "{file}:{line}:{column}"
Rider
custom command
```

- path quoting を安全にする。
- 未設定時は OS shell open fallback。
- compiler error と script exception stack trace から開ける。

## 必須機能 3: last-known-good 状態表示

`04` と統合する。

Editor に表示:

```text
Current source build status
Loaded assembly build ID
Last successful build time
Fallback 中か
ALC leak warning
Play 中変更 pending か
```

## 必須機能 4: Missing Script UI

`06` の Missing Script entry を Inspector に表示する。

UI:

```text
Missing Script
last-known type name
script type ID
script asset ID
script slot ID
保存済み field data
解決失敗理由
Remove ボタン
Reassign ボタン
```

Remove は Undo / Redo 対応。自動削除しない。

## 必須機能 5: Inspector attributes

`05` の全属性を Editor UI へ反映する。

```text
HideInInspector
Range
Min
Tooltip
Header
ReadOnly
Multiline
FormerlySerializedAs migration
DragSpeed
```

## 必須機能 6: runtime value Inspector

- Play 中 selected entity の managed runtime field を表示。
- authoring / runtime の表示を区別。
- runtime edit は Play instance のみに反映。
- optional `Apply To Authoring` button。
- Stop 後に authoring 値へ戻る。
- 全 script 全 field の毎 frame serialize を禁止。

## 必須機能 7: Undo / Redo

既存 Editor command framework を使う。

現行参照:

```text
Engine/Editor/Commands/Components/SetSerializedComponentCommand.*
Engine/Editor/Core/EditorManager.*
```

対応:

- script attach / remove
- script reorder
- script enabled toggle
- serialized field edit
- Missing Script remove / reassign
- reference picker change
- List add / delete / reorder
- runtime `Apply To Authoring`

## 必須機能 8: Script 作成テンプレート

ProjectPanel から作成する。

生成例:

```csharp
using NEMEngine;

[ScriptTypeId("<generated uuid>")]
public sealed class NewScript : ScriptBehaviour {
    public override void Awake() {
    }

    public override void Update() {
    }
}
```

要件:

- class 名 sanitize。
- 重複 file を避ける。
- stable UUID を生成。
- 作成後に IDE open。
- build queue へ通知。

## 必須機能 9: IDE project refresh

- GameScripts project path を Editor 設定から確認可能にする。
- generated schema / analyzer / ScriptCore reference が project に正しく入る。
- refresh command を追加。
- SDK-style glob で足りる場合も、manifest と generated dependency の検証を行う。

## 必須機能 10: Script Execution Order

C# attribute:

```csharp
[DefaultExecutionOrder(-100)]
```

さらに Editor override を必要に応じて追加する。

- lifecycle pass 内で execution order を使う。
- 同値 tie-breaker は deterministic。
- Inspector または project setting で確認可能。
- native behavior と managed behavior の整合を取る。

## 必須機能 11: Script exception から Entity 選択

structured diagnostic:

```text
script type ID
script full name
script slot ID
entity stable ID
scene instance ID
callback
exception
stack trace
```

ConsolePanel の行を click すると Entity を選択し、stack frame を IDE で開ける。

## 必須機能 12: Managed profiler

最低限:

```text
callback 別時間: Awake / Start / FixedUpdate / Update / LateUpdate / collision
script type 別時間
instance 別時間
呼び出し回数
例外回数
coroutine 実行時間
allocation 診断は可能な範囲
```

要件:

- Debug / Develop で詳細計測。
- Release は低コスト集計または無効化可能。
- frame profiler と統合。
- instance 別詳細は上位 N 件または選択対象のみでもよい。
- profiler 自身の allocation を抑える。

## 必須機能 13: constructor ルール analyzer

`ScriptBehaviour` constructor で避けるべき処理を検出する analyzer を追加する。

最低限 warning:

```text
Native API access
Entity / Scene access
StartCoroutine
Task.Run / Thread start
file IO
event subscription
```

誤検出が大きい場合は warning + documentation にする。

## 変更候補ファイル

```text
Engine/Editor/UI/Panels/Builtin/ConsolePanel.*
Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*
Engine/Editor/Scripting/*
Engine/Editor/Commands/*
Engine/Editor/Core/EditorManager.*
Engine/Core/Scripting/Managed/*
Engine/Managed/NEM.ScriptCore/*
Engine/Managed/NEM.ScriptAnalyzers/*                            (new 推奨)
```

## 回帰テスト

- syntax error の file / line / column を表示し IDE jump。
- Missing Script を削除せず保持し、reassign できる。
- script field edit、List reorder、reference picker change を Undo / Redo。
- Play 中 runtime field が更新され、Stop 後 authoring 値へ戻る。
- exception console click で Entity 選択。
- execution order が lifecycle log に反映。
- profiler で重い Update script が上位に出る。

## 完了チェックリスト

- [ ] Compiler Error List と IDE jump がある。
- [ ] Missing Script UI がある。
- [ ] authoring / runtime Inspector がある。
- [ ] script 編集操作が Undo / Redo 対応。
- [ ] script template が stable ID を生成する。
- [ ] execution order が動作する。
- [ ] exception から Entity と source へ移動できる。
- [ ] managed profiler がある。
