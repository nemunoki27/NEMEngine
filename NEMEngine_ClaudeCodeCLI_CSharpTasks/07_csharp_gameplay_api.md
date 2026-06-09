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

# 07. 実ゲーム制作向け C# Gameplay API を実装する

## 目的

小さなサンプルではなく、通常のゲームロジックを C# から記述できる API を揃える。DirectX 12 の低レベル object は C++ に閉じ込め、gameplay に必要な抽象だけを公開する。

## 前提

- `02`, `03`, `05`, `06`, `08`, `09`, `10` の基盤を先に実装する。
- 構造変更 API は `WorldCommandBuffer` 経由。
- component wrapper は generated registry を利用し、reflection を gameplay hot path に入れない。

## 7.1 Entity / Component API

最低限、以下を実装する。

```csharp
public readonly struct Entity : IEquatable<Entity> {
    public bool isValid { get; }
    public bool isAlive { get; }
    public string name { get; set; }
    public bool activeSelf { get; set; }
    public bool activeInHierarchy { get; }
    public Transform transform { get; }

    public bool Has<T>() where T : struct, IComponentRef;
    public bool TryGet<T>(out T component) where T : struct, IComponentRef;
    public T Get<T>() where T : struct, IComponentRef;
    public void Add<T>() where T : struct, IComponentRef;
    public void Remove<T>() where T : struct, IComponentRef;

    public bool TryGetScript<T>(out T script) where T : ScriptBehaviour;
    public ScriptRef<T> GetScriptRef<T>() where T : ScriptBehaviour;

    public void Destroy();
}
```

追加:

```text
Entity.Create
Entity.FindByUUID または EntityRef.Resolve
parent
root
firstChild
nextSibling
childCount
GetChild(index)
SetParent(parent, worldPositionStays)
```

要件:

- stale handle は安全に false / exception へ変換する。
- `Get<T>()` は component 不在時に明確な `MissingComponentException`。
- Add / Remove / Destroy / SetParent は deferred command。
- `Equals` と `GetHashCode` は world handle + entity index + generation を使う。

## 7.2 Transform API

既存 API を拡張し、すべて実装する。

```csharp
public readonly struct Transform {
    public Entity entity { get; }
    public Vector3 position { get; set; }
    public Vector3 localPosition { get; set; }
    public Quaternion rotation { get; set; }
    public Quaternion localRotation { get; set; }
    public Vector3 localScale { get; set; }
    public Vector3 lossyScale { get; }
    public Vector3 forward { get; }
    public Vector3 right { get; }
    public Vector3 up { get; }
    public Transform? parent { get; set; }
    public Transform root { get; }
    public int childCount { get; }

    public Transform GetChild(int index);
    public void SetParent(Transform? parent, bool worldPositionStays = true);
    public void Translate(Vector3 delta, Space space = Space.Self);
    public void Rotate(Vector3 eulerAngles, Space space = Space.Self);
    public void Rotate(Quaternion delta, Space space = Space.Self);
    public void LookAt(Vector3 target);
    public Vector3 TransformPoint(Vector3 point);
    public Vector3 InverseTransformPoint(Vector3 point);
    public Vector3 TransformDirection(Vector3 direction);
    public Vector3 InverseTransformDirection(Vector3 direction);
    public void SetPositionAndRotation(Vector3 position, Quaternion rotation);
}
```

### 性能設計

- `Transform` は allocation を避ける `readonly struct` wrapper 推奨。
- position と rotation の同時更新には batched native callback を用意する。
- transform snapshot 取得もまとめられる場合は `NativeTransformSnapshot` を使う。
- matrix 演算は native 側の既存 math utility を再利用する。
- DX12 object を C# に公開しない。

## 7.3 Native component binding

native component の調整可能な authoring parameter はすべて C# から操作可能にする。自動生成方式は `09_component_binding_codegen.md` に従う。

### 対象 component と公開方針

| Component | 公開する主な値 / 操作 |
|---|---|
| `TransformComponent` | 7.2 の Transform API |
| `SceneObjectComponent` | `activeSelf`, `activeInHierarchy`, visibility layer mask。永続 ID は read-only |
| `NameComponent` | name |
| `HierarchyComponent` | parent / child traversal。runtime link は read-only |
| `SpriteRendererComponent` | texture, material, size, pivot, color, layer, order, visible, blendMode, queue |
| `TextRendererComponent` | font, material, text, fontSize, charSpacing, color, layer, order, visible, blendMode, queue |
| `MeshRendererComponent` | mesh, material, layer, order, visible, blendMode, queue, enableZPrepass, subMeshes |
| `SubMeshMaterial` | texture 群、color、emissive、metallic、roughness、UV、local transform。runtime matrix は read-only または非公開 |
| `UVTransformComponent` | pos, rotation, scale。previous 値と matrix は非公開または read-only |
| `BillboardComponent` | axes |
| `InvertedHullOutlineComponent` | authoring field 全て |
| `ScreenSpaceOutlineComponent` | authoring field 全て |
| `OrthographicCameraComponent` | near/far、CameraCommon authoring field |
| `PerspectiveCameraComponent` | fov、near/far、CameraCommon authoring field |
| `CameraControllerComponent` | enabled、mode、follow、lookAt、shake authoring field、`RequestShake()` |
| `DirectionalLightComponent` | color、direction、intensity、shadowStrength、enabled、mask |
| `PointLightComponent` | color、intensity、radius、decay、enabled、mask |
| `SpotLightComponent` | color、direction、intensity、distance、decay、角度、enabled、mask |
| `AudioSourceComponent` | clip、enabled、playOnAwake、loop、volume、isPlaying read-only、Play / Stop |
| `SkinnedAnimationComponent` | enabled、loop、playbackSpeed、transitionDuration、clip、runtime state read-only、Play / CrossFade / Stop |
| `CollisionComponent` | 既存 authoring 設定。新しい physics query は作らない |
| `PrefabLinkComponent` | prefab asset、local ID、instance ID、root flag を read-only |
| `ScriptComponent` | gameplay からの直接 vector 編集は禁止。専用 script API を使う |

`runtime*` field は自動的に writable 公開しない。gameplay に有用なものだけ read-only property または method として curated 公開する。

## 7.4 Asset / Prefab / Scene

### AssetRef

```csharp
public readonly struct AssetRef<TAsset> where TAsset : IAssetType {
    public UUID id { get; }
    public bool isValid { get; }
}
```

asset marker type:

```text
TextureAsset
MaterialAsset
MeshAsset
SceneAsset
PrefabAsset
FontAsset
AudioClipAsset
AnimationClipAsset
CollisionSettingsAsset
PostProcessStackAsset
```

### Prefab

```csharp
public static class Prefab {
    public static DeferredEntity Instantiate(
        AssetRef<PrefabAsset> prefab,
        Vector3 position,
        Quaternion rotation,
        Entity parent = default);
}
```

構造変更は deferred。即時に component access できないため、`DeferredEntity` または予約済み Entity handle semantics を明示する。

推奨:

```csharp
public readonly struct DeferredEntity {
    public bool isResolved { get; }
    public Entity Resolve();
    public EntityRef ToReference();
}
```

command flush 後に resolve できる。予約 slot を採用する場合も、flush 前 access の失敗 semantics を明示する。

### Scene

```csharp
public static class SceneManager {
    public static DeferredSceneLoad LoadAdditive(AssetRef<SceneAsset> scene);
    public static void Unload(Scene scene);
    public static Scene activeScene { get; }
    public static IReadOnlyList<Scene> loadedScenes { get; }

    public static event Action<Scene> sceneLoaded;
    public static event Action<Scene> sceneUnloading;
    public static event Action<Scene> sceneUnloaded;
}
```

- native `SceneInstanceManager` を再利用する。
- Scene 操作は command buffer へ積む。
- scene event 順序は `03_lifecycle_multi_pass.md` に従う。
- event subscription は reload lifetime と連動する。

## 7.5 Collision / Physics

今回は新しい physics 基盤を作らない。

維持するもの:

```text
OnCollisionEnter
OnCollisionStay
OnCollisionExit
既存 Collision struct
既存 CollisionComponent authoring binding
```

見送るもの:

```text
Rigidbody
Physics.Raycast
OverlapSphere
OverlapBox
Trigger 専用 callback の再設計
```

native physics が整った段階で別指示書を作る。

## 7.6 Input API

### Raw input

既存 API を維持し、不足を追加する。

```text
GetKey / GetKeyDown / GetKeyUp
GetMouseButton / Down / Up
mousePosition
mouseDelta
mouseWheel
GetGamepadButton / Down / Up
leftStick / rightStick
leftTrigger / rightTrigger
gamepad connection
device ID
window focus
text input
UI に消費されたか
```

### Native InputSystem 拡張

- XInput device 0 固定をやめ、最低 4 gamepad slot を扱う。
- `GamepadId` を導入。
- button up を追加。
- per-device dead zone。
- vibration API を C# へ公開。
- input snapshot は frame ごとに一回 native 側で更新し、C# query は O(1)。
- Editor GameView / SceneView focus と UI consumption を区別する。

### Action map

```csharp
public static class InputActions {
    public static bool WasPressed(InputActionId action);
    public static bool WasReleased(InputActionId action);
    public static bool IsPressed(InputActionId action);
    public static float ReadValue(InputActionId action);
    public static Vector2 ReadVector2(InputActionId action);
    public static void Rebind(InputActionId action, InputBinding binding);
    public static void SaveProfile(string name);
    public static void LoadProfile(string name);
}
```

要件:

- binding asset または JSON config。
- keyboard、mouse、gamepad、composite WASD、stick、dead zone。
- runtime rebind。
- profile save / load。
- action name の文字列検索を毎 frame 行わず、起動時に ID 化する。

## 7.7 Time / Timer / Coroutine

### Time

```csharp
public static class Time {
    public static float time { get; }
    public static float unscaledTime { get; }
    public static float deltaTime { get; }
    public static float unscaledDeltaTime { get; }
    public static float fixedDeltaTime { get; }
    public static float timeScale { get; set; }
    public static ulong frameCount { get; }
}
```

### `TimeScaleComponent`

user 方針に合わせて追加する。

推奨 field:

```cpp
struct TimeScaleComponent {
    bool enabled = true;
    float scale = 1.0f;
    int32_t priority = 0;
};
```

複数存在時の解決規則を明示する。推奨:

- enabled な component のうち priority 最大を採用。
- tie は deterministic な entity ID 順。
- component 不在時は global `1.0f`。
- scale は 0 以上に clamp。
- Editor 時間と unscaled 時間は影響を受けない。
- gameplay `SystemContext.deltaTime` は scaled、別 field に unscaled delta を保持。
- fixed update accumulator の scale semantics を明確化し、テストする。

### Coroutine

```csharp
Coroutine StartCoroutine(IEnumerator routine);
void StopCoroutine(Coroutine coroutine);
void StopAllCoroutines();
```

対応 yield:

```text
yield return null
WaitForSeconds
WaitForSecondsRealtime
WaitForFixedUpdate
WaitForEndOfFrame
WaitUntil
WaitWhile
```

要件:

- ScriptCore 側 scheduler で管理。
- script instance、entity、world、assembly reload lifetime に紐付ける。
- destroy / Stop / reload で自動 cancel。
- 毎 frame の過剰 allocation を避け、node pool または compact list を使う。
- 例外は managed guard で捕捉し、対象 routine と script を診断する。

### Timer

```csharp
TimerHandle Schedule(float seconds, Action callback, bool unscaled = false);
TimerHandle ScheduleRepeating(float interval, Action callback, bool unscaled = false);
void Cancel(TimerHandle handle);
```

Coroutine と同じ lifetime 管理を使う。

## 7.8 Scene / application event

scene event は必須。

```text
sceneLoaded
sceneUnloading
sceneUnloaded
activeSceneChanged
```

application event は native event 起点が存在するものから追加する。

```text
OnApplicationFocus(bool)
OnApplicationPause(bool)
OnApplicationQuit()
```

## 変更候補ファイル

```text
Engine/Core/Scripting/Managed/ManagedScriptBridge*.cpp
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Core/World/Components/*
Engine/Core/World/Prefab/Runtime/PrefabSystem.*
Engine/Core/World/Scene/Runtime/SceneInstanceManager.*
Engine/Core/Platform/Input/InputSystem.*
Engine/Core/Foundation/Time/*
Engine/Core/World/ECS/Systems/Scheduler/*
Engine/Managed/NEM.ScriptCore/Runtime/*
```

## 回帰テスト

- C# だけで Entity 生成、component add、値変更、prefab instantiate、scene additive load ができる。
- Transform の world / local 操作、親変更、point conversion が正しい。
- 全 generated component wrapper で authoring property round-trip。
- gamepad 複数接続、button up、rebind profile が動作する。
- `timeScale=0`, `0.5`, `2.0` で scaled / unscaled time が期待通り。
- coroutine が destroy、Stop、reload で残らない。
- sceneLoaded が Awake / OnEnable 後、Start 前に届く。

## 完了チェックリスト

- [ ] Transform API が全件ある。
- [ ] native component の調整可能 parameter が C# から扱える。
- [ ] AssetRef / Prefab / Scene API がある。
- [ ] Input raw API と action map がある。
- [ ] TimeScaleComponent、Time、Timer、Coroutine がある。
- [ ] scene event がある。
- [ ] 新 physics API を勝手に実装していない。
