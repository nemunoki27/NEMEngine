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

# 08. この ECS に適した C# object model を実装する

## 目的

C++ ECS を唯一の data owner としたまま、C# 側では Unity に近い書き味を提供する。managed 側に component data の複製や native pointer を保持しない。

## 原則

- `Entity` は軽量な immutable handle wrapper。
- component wrapper は軽量な `readonly struct`。
- `ScriptBehaviour` だけが managed object として lifecycle と managed field を持つ。
- C# 側は ECSWorld pointer、component pointer、chunk pointer を保持しない。
- native component access は handle 検証を通す。
- component structural change は deferred command。

## `Entity`

```csharp
public readonly struct Entity : IEquatable<Entity> {
    internal readonly NativeEntity handle;

    public static Entity nullEntity { get; }
    public bool isValid { get; }
    public bool isAlive { get; }
    public UUID stableId { get; }
    public string name { get; set; }
    public bool activeSelf { get; set; }
    public bool activeInHierarchy { get; }
    public Transform transform => new(this);

    public bool Has<T>() where T : struct, IComponentRef;
    public bool TryGet<T>(out T component) where T : struct, IComponentRef;
    public T Get<T>() where T : struct, IComponentRef;
    public void Add<T>() where T : struct, IComponentRef;
    public void Remove<T>() where T : struct, IComponentRef;
    public void Destroy();
}
```

要件:

- `Entity` の作成だけで heap allocation しない。
- `Equals`, `GetHashCode`, `==`, `!=` を実装する。
- `ToString()` は debug 用に name / handle を返してよいが、hot path で呼ばない。
- invalid access は read API では安全な false または明確な例外方針を統一する。
- stale world generation を検出する。

## `IComponentRef`

```csharp
public interface IComponentRef {
    Entity entity { get; }
}
```

生成 wrapper 例:

```csharp
public readonly partial struct SpriteRenderer : IComponentRef {
    public Entity entity { get; }
    internal SpriteRenderer(Entity entity) => this.entity = entity;

    public AssetRef<TextureAsset> texture {
        get => ComponentApi.GetAsset<TextureAsset>(entity, ComponentIds.SpriteRenderer, PropertyIds.Texture);
        set => ComponentApi.SetAsset(entity, ComponentIds.SpriteRenderer, PropertyIds.Texture, value);
    }
}
```

## `ScriptBehaviour`

```csharp
public abstract class ScriptBehaviour {
    public Entity entity { get; internal set; }
    public Transform transform => entity.transform;
    public bool enabled { get; set; }

    protected bool TryGet<T>(out T component) where T : struct, IComponentRef;
    protected T Get<T>() where T : struct, IComponentRef;
    protected Coroutine StartCoroutine(IEnumerator routine);
    protected void StopCoroutine(Coroutine coroutine);
    protected void StopAllCoroutines();

    public virtual void Awake() {}
    public virtual void Start() {}
    public virtual void OnEnable() {}
    public virtual void OnDisable() {}
    public virtual void OnDestroy() {}
    public virtual void FixedUpdate() {}
    public virtual void Update() {}
    public virtual void LateUpdate() {}
}
```

要件:

- `enabled` は script slot ごとの状態を native `ScriptEntry.enabled` と同期する。
- constructor に gameplay 処理を書かない。
- component lookup helper は generated registry を使う。
- coroutine / timer は instance lifetime に紐付く。

## 参照モデル

### `AssetRef<T>`

永続 UUID と expected asset type を持つ。native resource object を直接保持しない。

### `EntityRef`

authoring stable reference を保持し、必要時に runtime Entity を解決する。runtime Entity handle と別型にする。

### `ScriptRef<T>`

EntityRef + script type ID + slot ID で特定する。managed object への強参照を scene data に保存しない。

## wrapper cache 方針

- `Transform` や generated component wrapper は struct なので通常 cache 不要。
- managed instance が頻繁に同じ wrapper を使う場合も、unsafe pointer cache は禁止。
- asset metadata 等、immutable なものだけ ID ベースで cache する。

## 例外方針

- `TryGet<T>`: component 不在や stale Entity で false。
- `Get<T>`: component 不在で `MissingComponentException`。
- stale world / entity: `InvalidEntityException` または safe false。API ごとに一貫させる。
- script callback 例外: `02_critical_runtime_stabilization.md` の fault 処理。

## compatibility

既存の Unity 風 lower camel API を維持する。

```text
entity
transform
position
localPosition
deltaTime
```

PascalCase alias を大量に追加して API を二重化しない。

## 変更候補ファイル

```text
Engine/Managed/NEM.ScriptCore/Runtime/Entity.cs
Engine/Managed/NEM.ScriptCore/Runtime/Transform.cs
Engine/Managed/NEM.ScriptCore/Runtime/ScriptBehaviour.cs
Engine/Managed/NEM.ScriptCore/Runtime/Components/*
Engine/Managed/NEM.ScriptCore/Runtime/References/*
Engine/Managed/NEM.ScriptCore/Runtime/Exceptions/*
Engine/Core/Scripting/Managed/ManagedScriptBridge*.cpp
```

## 回帰テスト

- `Entity` と component wrapper の通常 access で managed allocation が発生しない。
- Stop 後の Entity が新 Play world の同 index entity を誤参照しない。
- `TryGet<T>` と `Get<T>` の semantics が統一されている。
- 同一 Entity に同 type script を複数付与し、ScriptRef slot 解決が正しい。
- wrapper が native component pointer を保持していない。

## 完了チェックリスト

- [ ] Entity は世代付き lightweight wrapper。
- [ ] component は generated readonly struct wrapper。
- [ ] ScriptBehaviour.enabled が slot 状態と同期する。
- [ ] reference type が authoring / runtime を分離している。
- [ ] hot path に reflection と allocation がない。
