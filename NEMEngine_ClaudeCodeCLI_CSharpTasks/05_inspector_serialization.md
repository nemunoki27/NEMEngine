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

# 05. Inspector と script serialization を完成させる

## 目的

C# script field を、rename、参照、配列、nullable、長い JSON、Play 中 debug に耐える保存形式へ拡張する。

## 前提

- `06_script_manifest_stable_identity.md` の stable script type ID を使用する。
- `10_managed_instance_handle_generation.md` の script instance handle を使用する。
- `09_component_binding_codegen.md` と metadata 方針を揃える。

## 対応必須 field type

既存型に加えて、以下を対応する。

```text
enum
long
ulong
AssetRef<T>
EntityRef
ScriptRef<T>
List<T>
T[]
Nullable<T> / optional
prefab 内 Entity 参照
scene 内 Entity 参照
```

既存型も維持する。

```text
bool
int
float
double
string
Vector2
Vector3
Vector4
Quaternion
Color3
Color4
```

## 保存フォーマット

`ScriptEntry.serializedFields` は schema version と stable field ID を持つ形式へ移行する。例:

```json
{
  "schemaVersion": 2,
  "fields": {
    "a1f0...field-guid": {
      "name": "walkSpeed",
      "type": "float",
      "value": 4.0
    }
  },
  "unresolvedFields": {}
}
```

要件:

- 保存の主キーは field name ではなく stable field ID。
- 読み込み時は stable field ID、現在名、`FormerlySerializedAs` alias の順で解決する。
- 解決不能 field を即削除しない。`unresolvedFields` に保持し、round-trip で失わない。
- 旧形式 `{ "moveSpeed": 4.0 }` を読み込み時に移行する。
- 移行後の保存は新形式に統一する。
- type mismatch は silent cast せず warning を出し、安全に移行できる数値変換だけ許可する。

## Stable field ID

- stable script type ID + declaring type + serialization origin name を基に deterministic ID を生成する。
- rename 時は `[FormerlySerializedAs("oldName")]` chain を使って origin を維持する。
- 衝突や特殊移行用に明示属性 `[SerializedFieldId("uuid")]` を追加してよい。
- generator / schema cache で collision を検出し、build error にする。

## Inspector 属性

以下をすべて追加する。

```csharp
[SerializeField]
[HideInInspector]
[Range(min, max)]
[Min(min)]
[Tooltip("...")]
[Header("...")]
[ReadOnly]
[Multiline]
[FormerlySerializedAs("oldName")]
[DragSpeed(speed)]
```

### 属性 semantics

- `Range`: slider または clamp 付き drag。数値型へ適用。
- `Min`: 下限 clamp。
- `DragSpeed`: ImGui drag speed を指定。属性なしは型ごとの default。
- `ReadOnly`: 値を表示するが編集不可。
- `HideInInspector`: serializer 対象でも Inspector では非表示。
- `Multiline`: string の複数行編集。
- `Header`, `Tooltip`: Editor 表示専用。
- `FormerlySerializedAs`: 複数指定可能。migration 用。

## 参照型

### `AssetRef<T>`

```csharp
public readonly struct AssetRef<TAsset> where TAsset : IAssetType {
    public UUID id { get; }
    public bool isValid { get; }
}
```

要件:

- `TAsset` から native `AssetType` を解決する。
- Inspector は ProjectPanel drag & drop と picker に対応する。
- asset missing / type mismatch を明示する。
- UUID を保存する。

### `EntityRef`

scene / prefab authoring reference と runtime resolve を分離する。

保存例:

```json
{
  "kind": "Scene",
  "sourceAsset": "scene-guid",
  "localFileId": "entity-guid"
}
```

```json
{
  "kind": "Prefab",
  "sourceAsset": "prefab-guid",
  "localFileId": "prefab-local-guid"
}
```

runtime では scene instance ID または prefab instance ID と組み合わせて Entity handle へ解決する。runtime entity index を scene / prefab ファイルへ保存しない。

### `ScriptRef<T>`

```csharp
public readonly struct ScriptRef<T> where T : ScriptBehaviour {
    public EntityRef entity { get; }
    public UUID scriptTypeId { get; }
    public UUID scriptSlotId { get; }
}
```

同一 Entity に同じ script type が複数付く場合でも slot ID で一意に解決する。

## List / array / nullable

- recursive schema descriptor を用意する。
- List / array の element type を metadata に含める。
- Inspector で追加、削除、並べ替え、展開表示を行う。
- reference element にも対応する。
- nullable は null toggle と値 editor を表示する。
- 深すぎる再帰や循環型を schema build 時に拒否する。
- 一フレームで巨大 JSON を何度も parse しない。selected entity の編集時だけ serialize する。

## 固定長 buffer を廃止

削除対象:

- `ManagedNativeSerializedFieldInfo.name[128]`
- `displayName[128]`
- `defaultValueJson[512]`
- type name copy の固定長前提

推奨 API:

```text
GetScriptSchemaJsonSize(typeId, out size)
CopyScriptSchemaJson(typeId, buffer, capacity, out written)
GetRuntimeSerializedStateSize(instanceHandle, out size)
CopyRuntimeSerializedState(instanceHandle, buffer, capacity, out written)
```

または汎用 blob handle API を設計してもよい。

要件:

- 最初に必要 byte 数を取得し、呼び出し側が vector / byte[] を確保する二段階方式。
- UTF-8 と byte length を明示する。
- null terminator 必須かどうかを統一する。
- capacity 不足時に切り詰めず `BufferTooSmall` を返す。
- gameplay hot path ではこの API を使用しない。

## constructor ルール

初期値取得のために parameterless constructor を使うことは許可する。ただし次をルール化する。

- `ScriptBehaviour` constructor には gameplay 処理を書かない。
- native API、scene access、file IO、thread/task 開始、event subscription を行わない。
- gameplay 初期化は `Awake()` へ書く。
- analyzer で検出できる違反は `12_editor_scripting_tooling.md` で warning または error にする。

## Play モード runtime 値 Inspector

これは実装する。理由は、C# の `Update()` 内で変化した値を見られないと gameplay debug が困難になるため。

### 値を分離する

- Authoring value: scene / prefab に保存する値。
- Runtime value: Play world の managed instance が現在持つ値。

### UI ルール

- Edit 中: authoring value を編集し Undo / Redo と保存対象にする。
- Play 中: selected entity の runtime value を表示する。
- Play 中の編集: runtime instance のみに反映する。自動で scene へ保存しない。
- optional: `Apply Runtime Value To Authoring` button を付ける。明示操作時だけ Edit world へ反映する。
- readback は selected entity のみ、または表示中 field のみ。全 instance を毎 frame serialize しない。
- 10Hz 程度の throttling と、値編集直後の即時 refresh を組み合わせる。

## Script schema metadata

最低限:

```text
scriptTypeId
scriptSchemaVersion
fieldId
name
displayName
aliases
serializedKind
element metadata
nullable
isPublic
isReadOnly
isHidden
range
min
dragSpeed
tooltip
header
multiline
defaultValueJson
```

## 変更候補ファイル

```text
Engine/Core/World/Components/Scripting/ScriptComponent.*
Engine/Core/Scripting/Managed/ManagedScriptTypes.h
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/Serialization/*                 (new 推奨)
Engine/Managed/NEM.ScriptCore/Runtime/Attributes/*                    (new 推奨)
Engine/Managed/NEM.ScriptCore/Runtime/References/AssetRef.cs          (new)
Engine/Managed/NEM.ScriptCore/Runtime/References/EntityRef.cs         (new)
Engine/Managed/NEM.ScriptCore/Runtime/References/ScriptRef.cs         (new)
Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*
Engine/Editor/UI/Inspectors/Common/*
```

## 回帰テスト

- `moveSpeed` を `walkSpeed` へ rename し `[FormerlySerializedAs("moveSpeed")]` で値を維持する。
- enum、long、ulong、AssetRef、EntityRef、ScriptRef、List、array、nullable が save / reload で一致する。
- 512 byte を超える List と multiline string が切り詰められない。
- scene 内 EntityRef と prefab 内 EntityRef が instantiate 後に正しい runtime Entity へ解決する。
- missing asset、missing entity、missing script slot を Inspector で表示する。
- Play 中に script が field を変更すると runtime inspector に反映される。
- Play 中の runtime 編集が Stop 後に authoring value を勝手に上書きしない。
- unresolved field が save 後も失われない。

## 完了チェックリスト

- [ ] 指定された全 field type が round-trip する。
- [ ] `FormerlySerializedAs` が動作する。
- [ ] 全 Inspector 属性が実装されている。
- [ ] 固定長 buffer が廃止されている。
- [ ] runtime inspector が selected entity に対して動作する。
- [ ] scene / prefab reference が runtime index を永続化しない。
- [ ] 旧データ migration がある。
