<!-- BEGIN README.md -->

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

# NEMEngine C# スクリプト基盤 実装指示書セット

このフォルダーには、ユーザーが指定した `・` 区切りごとの Claude Code CLI 用実装指示書が入っています。各ファイルは単独でも読めますが、依存関係があるため原則として以下の順序で実装してください。

## 実装順

| 順番 | ファイル | 主目的 |
|---:|---|---|
| 1 | `02_critical_runtime_stabilization.md` | use-after-free、ABI、例外越境、構造変更、初期化失敗を先に修正 |
| 2 | `10_managed_instance_handle_generation.md` | managed instance handle の世代管理を完成 |
| 3 | `03_lifecycle_multi_pass.md` | lifecycle を複数 pass 化し、inactive hierarchy の Awake を遅延 |
| 4 | `11_nethost_hostfxr_standardization.md` | hostfxr 探索を標準化し、失敗経路を RAII 化 |
| 5 | `04_edit_mode_dll_reload.md` | Edit モード DLL reload を安全・非同期・復旧可能にする |
| 6 | `06_script_manifest_stable_identity.md` | script type の安定 GUID と manifest を導入 |
| 7 | `05_inspector_serialization.md` | serializer、属性、参照型、動的バッファ、runtime inspector を実装 |
| 8 | `08_csharp_object_model.md` | Entity / Component / ScriptBehaviour の公開モデルを確定 |
| 9 | `09_component_binding_codegen.md` | native component binding の自動生成を実装 |
| 10 | `07_csharp_gameplay_api.md` | Transform、component、Prefab、Scene、Asset、Input、Time、Coroutine を実装 |
| 11 | `12_editor_scripting_tooling.md` | Editor の診断、Missing Script、Profiler、テンプレート等を完成 |
| 12 | `13_full_game_readiness_scope.md` | 全体統合、未実装検出、縦切りテスト、完成判定 |

## 今回の明示的な非対象

- Play 中の GameScripts DLL ホットリロードは実装しない。Edit モード中の reload と Play 開始前 build / reload のみを対象にする。
- 新しい Rigidbody、Raycast、Overlap 系 Physics API は、native physics 基盤が整うまで実装しない。既存 collision callback と既存 `CollisionComponent` の binding は維持・整理する。

## 現行コードで確認済みの主要な起点

- Native managed runtime: `Engine/Core/Scripting/Managed/ManagedScriptRuntime.*`
- C# bridge: `Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs`, `NativeApi.cs`
- Script lifecycle: `Engine/Core/World/Systems/Behavior/BehaviorSystem.*`
- Native behavior store: `Engine/Core/World/Behavior/World/BehaviorWorld.*`
- ECS structural change: `Engine/Core/World/ECS/World/ECSWorld.*`
- Play / Edit switch: `Engine/Core/Runtime/Application/EngineApplication.cpp`
- Script inspector: `Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*`
- Script drag & drop: `Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.*`

## 完成の定義

各指示書のチェックリストがすべて満たされ、`13_full_game_readiness_scope.md` の統合テストを通過した時点で完了です。個別ファイルを実装しただけでは完了としません。

<!-- END README.md -->


<!-- BEGIN 02_critical_runtime_stabilization.md -->

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

# 02. ゲーム開発前の重大問題をすべて改善する

## 目的

C++ ECS と C# ScriptCore の境界を、安全で高速な基盤へ置き換える。公開 API の拡張前に、クラッシュ、古い参照、ABI 読み違い、ECS 反復破壊を封じ込める。

## 現状確認箇所

- `Engine/Core/Scripting/Managed/ManagedScriptRuntime.cpp`
  - `ScopedEnvironmentVariableOverride`
  - `Init`, `Finalize`, `LoadHostfxr`, `Invoke`, `InvokeCollision`
  - `CollectScriptSnapshotFiles`
- `Engine/Core/Scripting/Managed/ManagedScriptTypes.h`
  - `ManagedNativeEntity.world` が `std::uintptr_t`
  - `ManagedNativeApiTable`
  - `ManagedNativeSerializedFieldInfo` の固定長 buffer
- `Engine/Core/Scripting/Managed/ManagedScriptUtility.cpp`
  - `MakeNativeEntity`, `ResolveWorld`, `ResolveEntity`
- `Engine/Core/World/ECS/World/ECSWorld.*`
  - `AddComponent`, `RemoveComponent`, `MigrateEntity`
- `Engine/Core/World/Systems/Behavior/BehaviorSystem.cpp`
  - `Prepare()` 内で `world.ForEach<ScriptComponent>` 中に managed callback を実行
- `Engine/Core/Scripting/Managed/ManagedScriptBridgeEntity.cpp`
  - name / active 操作が必要に応じて component を即時追加し得る

## 必須実装 1: 環境変数復元時の不正 `free` を修正

`ScopedEnvironmentVariableOverride` では `_wdupenv_s` が返した領域を `std::wstring` へコピーした直後に解放する。デストラクタで `previousValue_.c_str()` を `free` してはいけない。

実装条件:

- `_wdupenv_s` の返却 pointer は constructor 内で必ず `std::free(previous)` する。
- destructor は `SetEnvironmentVariableW` による復元だけを行う。
- copy / move を禁止する。
- constructor 途中で失敗してもリークしない。

## 必須実装 2: 生の `ECSWorld*` を managed 側から排除

### 新しい ABI handle

`ManagedNativeEntity.world` を pointer として扱わない。次の考え方で世代付き handle に置き換える。

```cpp
struct ManagedWorldHandle {
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;
};

struct ManagedNativeEntity {
    ManagedWorldHandle world{};
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;
};
```

### `ManagedWorldRegistry`

新規追加例:

- `Engine/Core/Scripting/Managed/ManagedWorldRegistry.h`
- `Engine/Core/Scripting/Managed/ManagedWorldRegistry.cpp`

必要 API:

```cpp
ManagedWorldHandle Register(ECSWorld& world);
void Unregister(ManagedWorldHandle handle);
ECSWorld* TryResolve(ManagedWorldHandle handle);
ManagedWorldHandle TryGetHandle(const ECSWorld& world) const;
bool IsAlive(ManagedWorldHandle handle) const;
```

要件:

- slot 配列 + free list + generation による O(1) 解決にする。
- pointer は registry の native 内部にのみ保持する。
- `Unregister` 時に generation を増やす。
- 古い handle は `TryResolve == nullptr` になる。
- main thread 利用を基本とする。将来並列化しやすいよう、thread ownership assertion または明確なコメントを入れる。
- C# へ native pointer を返さない。

### World 登録タイミング

- Edit world と Play world が scripting から参照可能になる時点で登録する。
- Play world 破棄前に必ず unregister する。
- `WorldManager` に登録済み handle を保持するか、明示的な RAII registration object を持たせる。
- `MakeNativeEntity` は未登録 world を暗黙に登録せず、debug assertion + null handle を返す。登録漏れを隠さない。

### C# 側

`NativeEntity` を同じ ABI レイアウトへ更新する。`Entity.isValid` は pointer 判定ではなく、world handle と entity index の有効値で判定する。

## 必須実装 3: managed 例外を native 境界で完全に封じ込める

### 共通 status

C++ と C# で同じ値を共有する。

```cpp
enum class ManagedStatus : int32_t {
    Ok = 0,
    InvalidArgument,
    InvalidWorldHandle,
    InvalidEntityHandle,
    InvalidInstanceHandle,
    AbiMismatch,
    Unsupported,
    SerializationError,
    ScriptException,
    InternalError,
};
```

### 原則

- `[UnmanagedCallersOnly]` の export は例外を外へ出さない。
- constructor、reflection、JSON parse、field set、ユーザー callback、load / unload のすべてを guard 対象にする。
- `void` export は可能な限り `ManagedStatus` 戻り値へ変更する。
- 戻り値が必要な API は `ManagedStatus` + out parameter 形式にする。
- script callback の例外発生時は対象 script instance のみ faulted 状態にし、それ以降の gameplay callback を停止する。
- Editor と他 script は継続動作させる。

C# 側に共通 wrapper を実装する。

```csharp
private static ManagedStatus Guard(string apiName, Func<ManagedStatus> body)
private static ManagedStatus GuardInstance(NativeScriptInstanceHandle handle, string callbackName, Action<ScriptBehaviour> body)
```

ログには最低限、以下を含める。

- callback 名
- script type stable ID と full type name
- owner entity handle。解決できれば entity name と scene local ID
- 例外全文

native 側では status を確認し、`BehaviorRecord` に `faulted` と診断情報を保存する。

## 必須実装 4: ABI header と capability 検証

`ManagedNativeApiTable` の先頭に header を追加する。

```cpp
struct ManagedAbiHeader {
    uint32_t abiVersion;
    uint32_t structSize;
    uint64_t capabilities;
};
```

要件:

- `constexpr uint32_t kManagedAbiVersion = ...;` を native 側で定義する。
- managed 側も同一 version を要求する。
- `InitializeNativeApi` で version、minimum size、required capability を検証する。
- optional callback は capability bit と struct size で判定する。
- mismatch 時は関数 pointer を読み進めず、明確なログを出して初期化を拒否する。
- ABI 構造体には `static_assert(std::is_standard_layout_v<...>)` と必要な `sizeof` 検証を追加する。
- C# 側は `[StructLayout(LayoutKind.Sequential)]` を維持する。

## 必須実装 5: scripting 用 `WorldCommandBuffer`

### 背景

`BehaviorSystem::Prepare()` は `world.ForEach<ScriptComponent>` 走査中に managed callback を呼ぶ。callback 内で name、active、parent、component 追加削除、prefab instantiate 等を即時反映すると archetype 移動や hierarchy 変更で走査を壊し得る。

### 設計

新規追加例:

- `Engine/Core/World/ECS/World/WorldCommandBuffer.h`
- `Engine/Core/World/ECS/World/WorldCommandBuffer.cpp`

`ECSWorld` が command buffer を所有する。scripting から発生する構造変更は必ず queue に積む。

最低限の command:

```text
DestroyEntity
AddComponentByName
RemoveComponentByName
SetNameEnsuringComponent
SetActiveSelfEnsuringComponent
SetParent
InstantiatePrefab
LoadSceneAdditive
UnloadScene
```

実装条件:

- command は stable entity handle と必要な値をコピーして保持する。component への pointer や reference を保持しない。
- command 適用前に entity / world handle を再検証する。
- 同一 entity への重複 Destroy は安全に無視する。
- 同一 component の Add / Remove の競合ルールを定義し、適用順を deterministic にする。
- flush 中に追加 command が発生した場合は、無限ループを防ぐため現在 batch と次 batch を分ける。
- 一回の flush で許容する最大 iteration を設け、超過時は診断ログを出す。
- gameplay update 中の非構造的な既存 component 値変更は即時反映してよい。ただし component が存在しない場合の自動追加は queue 化する。
- command buffer の所有権は world に置き、Play world 破棄時に未処理 command を安全に破棄する。

flush 位置の詳細は `03_lifecycle_multi_pass.md` に従う。

## 必須実装 6: source monitor の除外

`CollectScriptSnapshotFiles` は再帰走査時に以下を除外する。

```text
bin
obj
.git
.vs
Generated
Library
Temp
*.g.cs
*.generated.cs
```

ただし、プロジェクト方針として source generator の入力に含めるディレクトリがある場合は、除外対象を設定ファイル化する。

追加要件:

- directory iterator の permission error で Editor 全体を落とさない。
- path normalize を行う。
- 同じ timestamp でも size が変わる環境を考慮し、必要なら size も snapshot に含める。
- 詳細な reload state machine は `04_edit_mode_dll_reload.md` で実装する。

## 必須実装 7: `hostfxr` 初期化失敗経路の cleanup

- `LoadLibraryW` 成功後のすべての失敗経路で `FreeLibrary` されること。
- `hostfxr_initialize_for_runtime_config` の context は scope guard で `hostfxr_close` する。
- `ManagedScriptRuntime::Init()` の途中失敗時にも半端な pointer を残さない。
- `Finalize()` は複数回呼んでも安全にする。
- 標準探索への移行は `11_nethost_hostfxr_standardization.md` で完成させる。

## 必須実装 8: invocation context を `thread_local` + RAII 化

現在の `currentContext_` 生 pointer 代入 / 手動 null 復元を置き換える。

要件:

- `thread_local const SystemContext*` を使用する。
- scope guard が以前の context を保存し、例外や早期 return があっても復元する。
- nested invocation を壊さない。
- callback が context 不在時に安全な default または `ManagedStatus::InternalError` を返す。
- main-thread-only API と将来 thread-safe にできる API をコメントで分類する。

## 変更候補ファイル

```text
Engine/Core/Scripting/Managed/ManagedScriptTypes.h
Engine/Core/Scripting/Managed/ManagedScriptRuntime.h
Engine/Core/Scripting/Managed/ManagedScriptRuntime.cpp
Engine/Core/Scripting/Managed/ManagedScriptUtility.h
Engine/Core/Scripting/Managed/ManagedScriptUtility.cpp
Engine/Core/Scripting/Managed/ManagedScriptBridge*.cpp
Engine/Core/Scripting/Managed/ManagedWorldRegistry.h                 (new)
Engine/Core/Scripting/Managed/ManagedWorldRegistry.cpp               (new)
Engine/Core/World/ECS/World/WorldCommandBuffer.h                      (new)
Engine/Core/World/ECS/World/WorldCommandBuffer.cpp                    (new)
Engine/Core/World/ECS/World/ECSWorld.h
Engine/Core/World/ECS/World/ECSWorld.cpp
Engine/Core/World/ECS/World/WorldManager.h
Engine/Core/World/ECS/World/WorldManager.cpp
Engine/Core/World/Behavior/World/BehaviorWorld.*
Engine/Core/World/Systems/Behavior/BehaviorSystem.*
Engine/Managed/NEM.ScriptCore/Runtime/NativeApi.cs
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/Entity.cs
```

## 性能要件

- entity / world handle 解決は O(1)。文字列検索を挟まない。
- high-frequency callback で JSON serialization を行わない。
- command enqueue は通常 O(1)。必要な payload だけをコピーする。
- Release build で過剰な stack trace 生成や全 entity 検査を常時行わない。

## 回帰テスト

- managed debugger 待機を有効にして Play 開始・停止を繰り返しても heap corruption が起きない。
- Play world の Entity を C# static field に保持し、Stop 後に `isAlive` を確認して false になる。
- 古い world handle が新しい Play world を参照しない。
- `Awake()` 内で `entity.name`、`SetActive`、`SetParent` を呼んでも走査が壊れない。
- script callback で例外を投げても Editor と他 script は継続する。
- ABI version を意図的に変えると安全に初期化拒否する。
- `obj` 内の generated `.cs` 更新で reload ループが起きない。
- 初期化失敗を意図的に起こした後でも再初期化または正常終了できる。

## 完了チェックリスト

- [ ] 生の `ECSWorld*` が managed ABI から消えている。
- [ ] すべての managed export が例外を封じ込める。
- [ ] ABI mismatch を検出できる。
- [ ] scripting 由来の構造変更は command buffer を通る。
- [ ] invocation context が RAII で復元される。
- [ ] source monitor が生成物を監視しない。
- [ ] failure path の native resource leak がない。
- [ ] 上記回帰テスト結果を報告できる。

<!-- END 02_critical_runtime_stabilization.md -->


<!-- BEGIN 10_managed_instance_handle_generation.md -->

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

# 10. Managed script instance handle に generation を追加する

## 目的

古い managed script ID が再利用後の別 instance を指さないようにする。長時間 Play、attach / detach、reload、destroy を安全にする。

## 現状の問題

`HostBridge.cs` は `List<ScriptBehaviour?> scripts` と単調増加 `nextScriptID` を使う。native `ManagedBehavior` は `int32_t managedHandle_` を保持する。世代検証と free list がない。

## ABI handle

```cpp
struct ManagedScriptInstanceHandle {
    uint32_t index = 0xFFFFFFFFu;
    uint32_t generation = 0;

    bool IsValid() const noexcept;
    static ManagedScriptInstanceHandle Null() noexcept;
};
```

C# 側にも同一 layout の struct を作る。

```csharp
[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeScriptInstanceHandle {
    public readonly uint index;
    public readonly uint generation;
}
```

## C# slot store

```csharp
private sealed class ScriptInstanceSlot {
    internal uint generation;
    internal ScriptBehaviour? instance;
    internal ScriptInstanceState state;
    internal ScriptTypeDescriptor descriptor;
}

private static readonly List<ScriptInstanceSlot> slots = new();
private static readonly Stack<uint> freeSlots = new();
```

### allocate

```text
1. free slot があれば pop
2. なければ slot 追加
3. instance を設定
4. 現在 generation を handle に入れて返す
```

### release

```text
1. index 範囲と generation を検証
2. coroutine / timer / tracked disposable を cancel
3. instance = null
4. state reset
5. generation++
6. generation overflow 時のルールを決める。0 を避けてもよい
7. free slot へ push
```

## export signature 更新

以下を `int handle` から `ManagedScriptInstanceHandle` へ変える。

```text
CreateInstance
SetSerializedFields
DestroyInstance
InvokeAwake
InvokeStart
InvokeOnEnable
InvokeOnDisable
InvokeOnDestroy
InvokeFixedUpdate
InvokeUpdate
InvokeLateUpdate
InvokeCollisionEnter
InvokeCollisionStay
InvokeCollisionExit
runtime inspector readback
```

すべて `ManagedStatus` を返す。

## native 更新

- `ManagedBehavior.managedHandle_` を新 handle にする。
- `0` sentinel を使わず `Null()` を使う。
- `ManagedScriptRuntime::Invoke*` の signature を更新。
- stale handle の callback は `InvalidInstanceHandle` として安全に無視し、診断を rate limit する。

## reload

- assembly unload 時は全 slot を release。
- reload 後に generation が衝突しないよう、store epoch または slot generation を維持する。
- より明確にする場合は handle に assembly epoch を加えてもよい。ただし ABI を簡潔に保つなら unload 時に全 generation を増やす。

## 性能要件

- allocate / release / lookup は償却 O(1)。
- gameplay callback lookup で dictionary や reflection を使わない。
- free list を使い、長時間 Play で slot vector が無制限に増えない。

## 変更候補ファイル

```text
Engine/Core/Scripting/Managed/ManagedScriptTypes.h
Engine/Core/Scripting/Managed/ManagedBehavior.h
Engine/Core/Scripting/Managed/ManagedBehavior.cpp
Engine/Core/Scripting/Managed/ManagedScriptRuntime.h
Engine/Core/Scripting/Managed/ManagedScriptRuntime.cpp
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/NativeApi.cs
```

## 回帰テスト

- 10 万回 create / destroy して slot 数が必要以上に増えない。
- release 済み handle で InvokeUpdate しても新 instance に届かない。
- 同 index 再利用後に generation が変わる。
- reload 前の handle が reload 後 instance に届かない。
- coroutine / timer が release で cancel される。

## 完了チェックリスト

- [ ] `int32_t managedHandle_` が残っていない。
- [ ] generation 検証が全 export にある。
- [ ] free list がある。
- [ ] stale handle が別 instance を指さない。
- [ ] reload epoch 相当の安全性がある。

<!-- END 10_managed_instance_handle_generation.md -->


<!-- BEGIN 03_lifecycle_multi_pass.md -->

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

# 03. Script lifecycle を複数 pass へ分割する

## 目的

同一 lifecycle boundary 内で、全対象 script の `Awake()` が完了する前に別 script の `Start()` が走らないようにする。inactive hierarchy の Entity は、有効化されるまで `Awake()` を遅延する。

## 前提

- `02_critical_runtime_stabilization.md` の `WorldCommandBuffer` が実装済みであること。
- `10_managed_instance_handle_generation.md` の世代付き managed instance handle を利用すること。

## 現状の問題

`Engine/Core/World/Systems/Behavior/BehaviorSystem.cpp` の `Prepare()` は、各 `ScriptEntry` ごとに次の順番で処理している。

```text
Create -> Apply fields -> Awake -> OnEnable / OnDisable -> Start -> 次 entry
```

このため、Entity A の `Start()` が Entity B の `Awake()` より前に実行され得る。また inactive hierarchy でも `Awake()` が呼ばれている。

## 必須 lifecycle 順序

ユーザー指定の順序を実装する。

```text
1. 必要な ScriptBehaviour インスタンスを全件生成
2. 対象スクリプトの Awake を全件実行
3. OnEnable / OnDisable を全件反映
4. SceneLoaded 通知
5. Start を全件実行
6. Update
7. WorldCommandBuffer を Flush
8. LateUpdate
9. WorldCommandBuffer を Flush
```

FixedUpdate にも安全地点を設ける。

```text
Lifecycle synchronize
FixedUpdate
WorldCommandBuffer Flush
```

## lifecycle 状態

`BehaviorRecord` に最低限、以下を持たせる。

```cpp
bool alive;
bool instanceCreated;
bool awakeCalled;
bool enabled;
bool startCalled;
bool faulted;
bool seen;
int32_t executionOrder;
Entity owner;
```

既存 `BehaviorRecord.instance` の有無で `instanceCreated` を兼ねてもよいが、状態遷移を読みやすく保つこと。

## `Prepare()` を分解する

単一の `Prepare()` に lifecycle callback を混在させない。例:

```cpp
void SynchronizeRecords(ECSWorld&, SystemContext&, bool sweep);
void CreatePendingInstances(ECSWorld&, SystemContext&);
void InvokePendingAwake(ECSWorld&, SystemContext&);
void ApplyEnableTransitions(ECSWorld&, SystemContext&);
void DispatchPendingSceneEvents(ECSWorld&, SystemContext&);
void InvokePendingStart(ECSWorld&, SystemContext&);
```

### Pass 1: record 同期と managed instance 生成

- `ScriptComponent` を走査し、型解決、record 作成、serialized field 適用、`seen` 更新を行う。
- C# object の constructor は呼んでよいが、gameplay 処理を書かないルールを維持する。
- callback はまだ呼ばない。
- inactive hierarchy の Entity でも instance は生成してよい。ただし `Awake` は呼ばない。
- participant handle の snapshot を作る。pass 実行中に record 配列が変化しても、同一 pass の反復対象を不意に増減させない。

### Pass 2: `Awake`

- `!awakeCalled && activeInHierarchy` の record だけ対象にする。
- `entry.enabled == false` でも hierarchy が active なら `Awake()` は実行する。script 自身の enabled は `OnEnable` と gameplay update を制御する。
- inactive hierarchy では `Awake()` を遅延する。
- callback で例外が発生したら record を faulted にし、残り callback を停止する。
- callback 内の構造変更は command buffer に queue するだけで即時適用しない。

### Pass 3: `OnEnable` / `OnDisable`

`shouldBeEnabled` は次で判定する。

```cpp
entry.enabled && activeInHierarchy && awakeCalled && !faulted
```

- false -> true で `OnEnable()` を一回呼ぶ。
- true -> false で `OnDisable()` を一回呼ぶ。
- 一度も `Awake()` していない inactive entity に `OnDisable()` は呼ばない。
- fault 発生時、以前 enabled なら安全に disable 状態へ遷移する。例外を起こした instance に追加 callback を呼ぶかは明示ルール化し、原則として呼ばず native 状態だけ false にする。

### Pass 4: Scene event

- scene load により生成された active script の `Awake` / `OnEnable` が完了した後に `SceneLoaded` を通知する。
- event 自身が command を queue しても即時構造変更しない。
- additive load に対応する。
- scene event の API 詳細は `07_csharp_gameplay_api.md` に従う。

### Pass 5: `Start`

- `enabled && awakeCalled && !startCalled && !faulted` の record に一回だけ呼ぶ。
- 全対象 `Awake()` 完了後に開始する。
- 一度 inactive になって再有効化されても `Start()` を再実行しない。

### Pass 6-9: Update / Flush / LateUpdate / Flush

- `Update()` 参加者 snapshot を固定する。
- Update 後に world command を flush する。
- `LateUpdate()` 参加者 snapshot を固定する。
- LateUpdate 後に world command を flush する。
- Update 後 flush で新規生成された Entity の script は、同フレーム LateUpdate へ途中参加させない。次の lifecycle synchronization boundary から参加させる。これにより deterministic な順序を維持する。

## FixedUpdate

`SystemScheduler` の各 fixed sub-step 後に command buffer を flush する。複数 sub-step の間で破棄や生成が確定する必要がある。

推奨:

```text
for each fixed sub-step:
  synchronize lifecycle if dirty
  systems FixedUpdate
  FlushWorldCommands
```

ただし、全 system の fixed update 中に同じ world snapshot を維持し、flush は system ごとの途中では行わない。

## Scene load / unload と destroy の順序

### Destroy

- enabled な script: `OnDisable()` -> `OnDestroy()` -> managed instance release
- Awake 未実行の inactive script: managed instance release のみ。`OnDestroy()` は呼ばない。
- Awake 実行済みで現在 disabled: `OnDestroy()` -> release

### Scene unload

推奨順序:

```text
SceneUnloading event
対象 Entity の OnDisable
対象 Entity の OnDestroy
Entity destroy flush
SceneUnloaded event
```

Scene event API は `07_csharp_gameplay_api.md` と統合する。

## 実行順序の安定化

- record の反復順に偶然依存しない。
- script execution order を導入する。詳細は `12_editor_scripting_tooling.md`。
- 同一 execution order の tie-breaker は deterministic にする。推奨は scene local ID、entity handle、script slot ID の順。
- native `MonoBehavior` と managed `ScriptBehaviour` の両方で同じ基本ルールを適用する。

## 変更候補ファイル

```text
Engine/Core/World/Systems/Behavior/BehaviorSystem.h
Engine/Core/World/Systems/Behavior/BehaviorSystem.cpp
Engine/Core/World/Behavior/World/BehaviorWorld.h
Engine/Core/World/Behavior/World/BehaviorWorld.cpp
Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h
Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.cpp
Engine/Core/World/ECS/World/ECSWorld.*
Engine/Core/Scripting/Managed/ManagedBehavior.*
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/ScriptBehaviour.cs
```

## 回帰テスト

- Script A と B を作り、ログが `Awake A`, `Awake B`, `Start A`, `Start B` の pass 順になる。
- hierarchy root inactive で Play 開始し、子 script の constructor は生成可能だが `Awake`, `OnEnable`, `Start` は呼ばれない。
- root を active にすると `Awake -> OnEnable -> Start` が一回ずつ呼ばれる。
- active -> inactive -> active で `OnDisable -> OnEnable` になり、`Awake`, `Start` は増えない。
- Update 内で prefab を instantiate しても、新規 script が同一フレーム LateUpdate に途中参加しない。
- FixedUpdate 内で destroy しても次 sub-step で破棄済みとして扱える。
- scene additive load 時に、新規 entity の Awake / OnEnable 後、Start 前に SceneLoaded が通知される。

## 完了チェックリスト

- [ ] lifecycle callback が単一 entry ループ内で混在していない。
- [ ] 全 `Awake` が全 `Start` より前に完了する。
- [ ] inactive hierarchy の `Awake` が遅延される。
- [ ] Update / LateUpdate 後に command flush がある。
- [ ] fixed sub-step 後にも command flush がある。
- [ ] deterministic な反復順がある。
- [ ] destroy と scene unload の callback 順がテストされている。

<!-- END 03_lifecycle_multi_pass.md -->


<!-- BEGIN 11_nethost_hostfxr_standardization.md -->

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

# 11. `nethost` を使って hostfxr 探索を標準化する

## 目的

手動で `C:/Program Files/dotnet/host/fxr` を走査する実装をやめ、.NET 公式 hosting API の `get_hostfxr_path` を利用する。初期化失敗時の cleanup と配布時診断を完成させる。

## 現状の問題

`ManagedScriptRuntime.cpp` の `FindDotnetRoot`, `FindHostfxrPath`, version directory 走査は、環境差、architecture 差、配布構成差に弱い。

## 公式 hosting flow

native host は次の flow にする。

```text
1. nethost の get_hostfxr_path で hostfxr path を得る
2. hostfxr.dll を LoadLibraryW
3. hostfxr_initialize_for_runtime_config
4. hostfxr_get_runtime_delegate
5. hdt_load_assembly_and_get_function_pointer を取得
6. hostfxr_close(context)
7. ScriptCore bridge export を取得
```

## 依存ファイル

公式 hosting header を vendor または取得する。

```text
nethost.h
hostfxr.h
coreclr_delegates.h
nethost.lib / nethost.dll または適切な dynamic loading 構成
```

配置例:

```text
Engine/Externals/dotnet-hosting/include/
Engine/Externals/dotnet-hosting/lib/x64/
```

実際の repository 構成に合わせる。

## 実装方針

### `DotnetHostResolver`

新規 service へ分離する。例:

```text
Engine/Core/Scripting/Managed/DotnetHostResolver.h
Engine/Core/Scripting/Managed/DotnetHostResolver.cpp
```

必要 API:

```cpp
std::filesystem::path ResolveHostfxrPath(const std::filesystem::path& assemblyOrRuntimeConfig);
bool LoadHostfxr(...);
```

### dynamic buffer

`get_hostfxr_path` は必要 buffer size を問い合わせ、dynamic `std::vector<wchar_t>` で再試行する。固定 `MAX_PATH` 前提にしない。

### runtimeconfig

- `NEM.ScriptCore.runtimeconfig.json` の存在を検証。
- framework-dependent deployment と self-contained deployment のどちらを採用するか明文化。
- 開発環境では SDK / runtime 欠落時に明確な error。
- 配布時に runtime 同梱する場合の探索先を config 化。

### RAII

wrapper 例:

```text
UniqueLibraryHandle
UniqueHostfxrContext
```

要件:

- すべての failure path で `FreeLibrary`。
- context は必ず `hostfxr_close`。
- `ManagedScriptRuntime::Finalize()` 複数回呼び出し安全。
- function pointer を library unload 後に使わない。

## architecture 検証

- 現在の project は x64 構成。x64 native host と一致する hostfxr を使う。
- mismatch の場合は「x64 / arm64 / x86」の情報を含む error を出す。
- 将来 architecture 追加時に resolver を差し替えやすくする。

## build project 更新

- `Engine/NEMEngine.vcxproj`
- `Engine/NEMEngine.vcxproj.filters`
- Sandbox executable 側で必要な link / copy step

静的 library 側だけで完結しない場合、最終 executable の linker input と runtime copy も更新する。

## logging

最低限:

```text
runtimeconfig path
resolved hostfxr path
native process architecture
hostfxr initialize result code
runtime delegate result code
ScriptCore assembly path
```

Release では個人環境 path の露出レベルを調整する。

## 公式資料

実装時は Microsoft Learn の custom .NET runtime hosting guide を確認する。

- Custom .NET runtime host: `https://learn.microsoft.com/dotnet/core/tutorials/netcore-hosting`
- Assembly unloadability: `https://learn.microsoft.com/dotnet/standard/assembly/unloadability`

## 回帰テスト

- SDK / runtime が正常な開発機で ScriptCore load 成功。
- `runtimeconfig.json` 欠落で明確な error。
- hostfxr 解決失敗で crash せず終了。
- 初期化途中 failure 後に `Finalize()` しても crash しない。
- path に空白があっても動作。
- x64 構成で x64 hostfxr が選ばれる。

## 完了チェックリスト

- [ ] `FindDotnetRoot` と version directory 手動走査に依存しない。
- [ ] `get_hostfxr_path` を使用する。
- [ ] RAII cleanup がある。
- [ ] runtimeconfig と architecture 診断がある。
- [ ] executable 側の link / copy 設定も確認済み。

<!-- END 11_nethost_hostfxr_standardization.md -->


<!-- BEGIN 04_edit_mode_dll_reload.md -->

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

# 04. Edit モード DLL reload を安全に完成させる

## 目的

GameScripts の編集時 reload を、Editor を固めず、古い DLL に復旧でき、load context leak を診断できる状態にする。

## 非対象

- Play 中 hot reload は実装しない。
- Play 中は source change を記録しても reload しない。Editor に「Stop 後に反映」または「次回 Play 開始時に build」表示を出すだけにする。

## 現状の問題

`ManagedScriptRuntime::AutoRebuildOnScriptChanges()` は、変更検知後に DLL を unload してから `_wsystem(dotnet build ...)` を同期実行する。これにより Editor 停止、復旧性低下、監視ループ、ALC leak の見逃しが起き得る。

## 必須実装 1: reload state machine

`ManagedScriptRuntime` または専用 `ManagedScriptBuildService` に明示状態を持たせる。

```text
Idle
Debouncing
Building
BuildSucceeded
BuildFailed
Staging
ReloadPending
Reloading
ReloadSucceeded
ReloadFailed
```

要件:

- Edit モードの frame tick で state を進める。
- build 中に Editor main thread を block しない。
- 追加変更が来たら dirty flag を立て、現在 build 完了後に再 build する。
- debounce を導入する。推奨 300-750ms の設定値。
- Play 開始操作時は、必要であれば build 完了を明示的に待つかキャンセルする。UI に状態を表示する。

## 必須実装 2: 非同期 build process

Windows の process API で `dotnet build` を子 process として起動し、stdout / stderr を redirect する。

- `_wsystem` を通常 reload 経路から排除する。
- process handle を RAII で管理する。
- build log を逐次 Editor console へ送る。
- exit code を取得する。
- Editor 終了時は process を適切に待つか終了する。
- `DOTNET_CLI_UI_LANGUAGE=en` は process environment block で設定する。
- 引数 quoting を安全にする。

Play 開始前の build は同期完了が必要だが、共通 process runner を使い、UI が応答可能な形にする。

## 必須実装 3: source 監視の除外と debounce

`02_critical_runtime_stabilization.md` の除外を必ず入れる。

推奨構成:

- `std::filesystem` polling を fallback として残す。
- Windows `ReadDirectoryChangesW` または `FileSystemWatcher` 相当を専用 service で使ってもよい。
- 変更 event は path set へ集約する。
- `bin`, `obj`, generated 出力、shadow copy 出力を監視しない。

## 必須実装 4: staging と last-known-good

build 成功前に現在ロード中 assembly を unload しない。

推奨ディレクトリ:

```text
<GameRoot>/Managed/Staging/<profile>/<build-id>/
<GameRoot>/Managed/LastKnownGood/<profile>/
<GameRoot>/Managed/Shadow/<profile>/<reload-id>/
```

手順:

```text
1. source change 検知
2. staging 出力へ dotnet build
3. DLL, PDB, deps.json, runtimeconfig 等の必要物を検証
4. manifest 生成・検証
5. last-known-good を更新可能な状態にする
6. 現在 assembly unload
7. staging から shadow copy を作成
8. shadow copy を collectible ALC へ load
9. type refresh 成功後に active build を切り替える
10. 失敗時は last-known-good の shadow copy を load して復旧
```

要件:

- 実行中 DLL を直接上書きしない。
- build failure では現在の正常 DLL を維持する。
- reload failure でも復旧を試みる。
- 古い shadow directory は保持数を決めて掃除する。
- Play 中は active assembly を差し替えない。

## 必須実装 5: ALC unload 診断

C# `HostBridge.ReleaseGameAssembly` を改善する。

- unload 対象 `GameScriptLoadContext` の `WeakReference` を作る。
- ALC への strong reference が scope 外へ出る構造にする。
- `Unload()` 後に限定回数の `GC.Collect()`, `GC.WaitForPendingFinalizers()` を行う。
- weak reference が残る場合は warning を出す。
- warning には reload 回数、load context 名、疑わしい原因を含める。
- GC loop は無制限に回さない。

## 必須実装 6: managed lifetime cancellation

古い GameScripts assembly を参照し続ける task、event、timer を止める。

ScriptCore 側に assembly lifetime service を追加する。例:

```csharp
public static class ScriptRuntimeLifetime {
    public static CancellationToken reloadToken { get; }
    internal static void BeginAssemblyLifetime();
    internal static void EndAssemblyLifetime();
    public static IDisposable Register(IDisposable disposable);
}
```

要件:

- unload 前に cancellation token を cancel。
- ScriptCore が所有する coroutine / timer を停止。
- 登録済み `IDisposable` を dispose。
- static event subscription を登録解除する helper を用意する。
- user code が追跡外 static reference を残した場合、ALC leak warning で検知する。

## 必須実装 7: native dependency resolver

`GameScriptLoadContext` に `LoadUnmanagedDll` override を追加する。

- `AssemblyDependencyResolver.ResolveUnmanagedDllToPath` を使う。
- path が解決できた場合だけ `LoadUnmanagedDllFromPath`。
- managed dependency の既存 `Load()` と同じ責務で扱う。
- ScriptCore 本体は default context の assembly を共有する。

## 必須実装 8: reload diagnostics

Editor console に以下を構造化して出す。

```text
build id
reload id
changed source path count
build duration
load duration
script type count
manifest validation result
ALC unload success / failure
fallback 実施有無
```

`12_editor_scripting_tooling.md` の Compiler Error List と統合する。

## 変更候補ファイル

```text
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Core/Scripting/Managed/ManagedScriptBuildService.*       (new 推奨)
Engine/Core/Scripting/Managed/ManagedProcessRunner.*            (new 推奨)
Engine/Core/Runtime/Application/EngineApplication.cpp
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/ScriptRuntimeLifetime.cs  (new)
Engine/Editor/UI/Panels/Builtin/ConsolePanel.*
```

## 回帰テスト

- `.cs` を連続保存しても build storm が起きない。
- `obj` 更新で rebuild が再帰発火しない。
- syntax error を入れても直前の正常 DLL と Inspector schema が維持される。
- 正常化して保存すると reload できる。
- 100 回 reload して ALC が回収される。
- 意図的に static event reference を残すと leak warning が出る。
- native dependency を持つ test assembly が resolver 経由で load できる。
- Play 中の保存では reload されず、Stop 後または次 Play 前に反映される。

## 完了チェックリスト

- [ ] `_wsystem` による通常 Editor block がない。
- [ ] build 前 unload を行わない。
- [ ] staging / shadow copy / last-known-good が動作する。
- [ ] source monitor に除外と debounce がある。
- [ ] ALC leak を検出できる。
- [ ] reload lifetime cancellation がある。
- [ ] Play 中 reload が無効である。

<!-- END 04_edit_mode_dll_reload.md -->


<!-- BEGIN 06_script_manifest_stable_identity.md -->

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

# 06. Script type 識別をファイル名依存から stable manifest へ移行する

## 目的

`.cs` ファイル名、単純 class 名、namespace に依存せず script を識別する。class rename、namespace rename、ファイル移動、1 ファイル複数 class に耐える。

## 現状の問題

`Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.cpp` は script asset path の stem を class 名として解決する。`ScriptEntry` は `std::string type` を保存主キーとしている。

## Stable Script Type ID

各 concrete `ScriptBehaviour` に UUID を持たせる。

```csharp
[ScriptTypeId("0d2f...uuid")]
public sealed class PlayerController : ScriptBehaviour {
}
```

要件:

- ID は rename、namespace change、file move で不変。
- script template 作成時に UUID を自動生成する。
- 1 ファイルに複数 concrete script があれば各 type が別 ID を持つ。
- ID 重複は build error。
- attribute がない script は Editor で明示 warning。移行期間のみ deterministic fallback ID を生成可能だが、shipping / completion 時には全 concrete script に明示 ID があること。

## Script Manifest

build 後に manifest を生成する。

```json
{
  "manifestVersion": 1,
  "assembly": "GameScripts",
  "buildId": "...",
  "scripts": [
    {
      "scriptTypeId": "...",
      "fullTypeName": "Game.Player.PlayerController",
      "displayName": "Player Controller",
      "sourcePath": "Scripts/PlayerController.cs",
      "sourceAssetId": "...",
      "schemaVersion": 2,
      "defaultExecutionOrder": 0
    }
  ]
}
```

## 推奨生成方式

Roslyn source generator を追加する。

例:

```text
Engine/Managed/NEM.ScriptCodeGen/
Sandbox/Scripts/GameScripts.csproj から Analyzer として参照
```

generator は compile 時に concrete `ScriptBehaviour` を列挙し、生成 C# registry を出す。

```csharp
internal static partial class GeneratedScriptManifest {
    internal static ReadOnlySpan<ScriptTypeDescriptor> descriptors => ...;
}
```

`HostBridge` は reflection 全走査ではなく、生成 registry を優先して読む。開発移行用 fallback reflection は warning 付きで限定的に残してよい。

build service は registry から `GameScripts.manifest.json` を staging へ出力し、native 側が load 前後に検証する。

## `ScriptEntry` の変更

保存主キーを stable ID にする。

```cpp
struct ScriptEntry {
    UUID scriptSlotID{};
    UUID scriptTypeID{};
    AssetID scriptAsset{};
    std::string lastKnownTypeName;
    bool enabled = true;
    nlohmann::json serializedFields = nlohmann::json::object();

    // runtime cache only
    BehaviorHandle handle = BehaviorHandle::Null();
    uint32_t resolvedRuntimeTypeID = 0;
    bool resolvedRuntimeTypeValid = false;
};
```

要件:

- `type` 文字列は保存主キーとして使わない。
- display と legacy migration 用に last-known name を保存してよい。
- `scriptSlotID` は同じ entity に同 type を複数 attach しても識別できる stable UUID。
- runtime type ID は reload ごとに変わってよい。scene / prefab に保存しない。

## Registry

`BehaviorTypeRegistry` を拡張する。

必要検索:

```text
FindByStableScriptTypeID(UUID)
FindManagedByFullName(string)          // legacy migration / display only
FindManagedBySimpleName(string)        // legacy migration only。曖昧なら失敗
FindBySourceAsset(AssetID)             // drag drop picker 用
```

要件:

- runtime hot path は UUID または数値 ID で解決する。
- simple name が複数候補なら自動選択しない。
- manifest validation failure は明示ログ。

## Drag & Drop

`ScriptAssetDragDrop` は asset path stem を使わない。

- script asset ID -> manifest の source asset mapping で候補 type を得る。
- 候補 1 件なら attach。
- 複数件なら picker popup を出す。
- 0 件なら「このファイルに attach 可能な ScriptBehaviour がない」と表示。
- manifest load 不可なら build error 状態を表示。

## Legacy migration

旧 scene / prefab の `type` 文字列を読み込む。

```text
1. full name 一致
2. 単純名が一意なら一致
3. source script asset mapping が一意なら一致
4. 解決不能なら Missing Script としてデータ保持
```

migration 後保存時に stable type ID を書く。

## Missing Script

解決不能でも entry を削除しない。

表示情報:

```text
last-known type name
script asset ID
script type ID
script slot ID
serialized field JSON
解決失敗理由
```

再び script が存在するようになったら自動復旧可能にする。

## 変更候補ファイル

```text
Engine/Core/World/Components/Scripting/ScriptComponent.*
Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.*
Engine/Core/Scripting/Managed/ManagedScriptRuntime.*
Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs
Engine/Managed/NEM.ScriptCore/Runtime/Attributes/ScriptTypeIdAttribute.cs       (new)
Engine/Managed/NEM.ScriptCore/Runtime/Metadata/ScriptTypeDescriptor.cs         (new)
Engine/Managed/NEM.ScriptCodeGen/*                                             (new)
Engine/Editor/Scripting/DragDrop/ScriptAssetDragDrop.*
Engine/Editor/UI/Inspectors/Builtin/ScriptInspectorDrawer.*
```

## 回帰テスト

- class rename 後も既存 scene の script が維持される。
- namespace rename 後も維持される。
- `.cs` file move と file rename 後も維持される。
- 1 file に 2 script class がある場合、drag drop で picker が出る。
- 同一 simple name が別 namespace にある場合、legacy simple-name migration が曖昧エラーになる。
- ScriptTypeId 重複で build error。
- script 削除後に Missing Script と値が残り、復元後に再接続される。

## 完了チェックリスト

- [ ] scene / prefab の保存主キーが stable script type ID。
- [ ] drag drop が filename stem に依存しない。
- [ ] script slot ID がある。
- [ ] manifest が staging build と一緒に生成・検証される。
- [ ] Missing Script が値を保持する。
- [ ] legacy scene migration がある。

<!-- END 06_script_manifest_stable_identity.md -->


<!-- BEGIN 05_inspector_serialization.md -->

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

<!-- END 05_inspector_serialization.md -->


<!-- BEGIN 08_csharp_object_model.md -->

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

<!-- END 08_csharp_object_model.md -->


<!-- BEGIN 09_component_binding_codegen.md -->

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

# 09. Native component binding を自動生成する

## 目的

native component の調整可能 parameter をすべて C# へ公開しつつ、手書き callback table の肥大化と追加漏れを防ぐ。

## 前提

- `08_csharp_object_model.md` の `IComponentRef` wrapper を採用する。
- Transform は hot path 専用の batched bridge を残してよい。

## 設計方針

単一の宣言的 binding schema を source of truth にする。例:

```text
Engine/Tools/ManagedBindings/ComponentBindings.json
```

または C++ descriptor DSL でもよい。ただし generated output と手書き schema の責務を明確にする。

schema 例:

```json
{
  "managedName": "SpriteRenderer",
  "nativeType": "Engine::SpriteRendererComponent",
  "componentId": "stable-guid-or-stable-integer",
  "properties": [
    { "name": "texture", "nativeField": "texture", "kind": "AssetRef<TextureAsset>", "access": "ReadWrite" },
    { "name": "visible", "nativeField": "visible", "kind": "Bool", "access": "ReadWrite" }
  ]
}
```

## 生成物

例:

```text
Engine/Core/Scripting/Managed/Generated/GeneratedManagedComponentBindings.h
Engine/Core/Scripting/Managed/Generated/GeneratedManagedComponentBindings.cpp
Engine/Managed/NEM.ScriptCore/Runtime/Generated/GeneratedComponents.g.cs
Engine/Managed/NEM.ScriptCore/Runtime/Generated/GeneratedComponentIds.g.cs
```

`Generated` は手編集禁止コメントを先頭に付ける。

## ABI の拡張方式

property ごとに `ManagedNativeApiTable` の field を増やさない。汎用 typed accessor を使う。

例:

```cpp
ManagedStatus HasComponent(ManagedNativeEntity, uint32_t componentID, int32_t* outHas);
ManagedStatus EnqueueAddComponent(ManagedNativeEntity, uint32_t componentID);
ManagedStatus EnqueueRemoveComponent(ManagedNativeEntity, uint32_t componentID);

ManagedStatus GetBoolProperty(ManagedNativeEntity, uint32_t componentID, uint32_t propertyID, int32_t* out);
ManagedStatus SetBoolProperty(...);
ManagedStatus GetIntProperty(...);
ManagedStatus SetIntProperty(...);
ManagedStatus GetFloatProperty(...);
ManagedStatus SetFloatProperty(...);
ManagedStatus GetVector2Property(...);
ManagedStatus SetVector2Property(...);
ManagedStatus GetVector3Property(...);
ManagedStatus SetVector3Property(...);
ManagedStatus GetColor4Property(...);
ManagedStatus SetColor4Property(...);
ManagedStatus GetAssetRefProperty(...);
ManagedStatus SetAssetRefProperty(...);
```

string、list、nested struct は動的 blob または専用 collection API を使う。

### 性能

- component ID と property ID は build 時定数。
- native 側は `unordered_map<string>` ではなく generated dense table または switch で O(1) dispatch。
- property accessor は entity handle 検証後、`TryGetComponent<T>` で参照。
- string / list 以外は heap allocation なし。
- Transform の高頻度処理は専用 API を優先。

## schema の field 分類

各 field を分類する。

```text
AuthoringReadWrite
RuntimeReadOnly
RuntimeCommand
InternalOnly
```

例:

- `AudioSourceComponent.volume`: AuthoringReadWrite
- `AudioSourceComponent.runtimeVoiceID`: InternalOnly
- `AudioSourceComponent.runtimePlaying`: RuntimeReadOnly
- `CameraControllerComponent.shake.runtimeTime`: InternalOnly
- `SkinnedAnimationComponent.runtimeCurrentClip`: RuntimeReadOnly
- `MeshRendererComponent.subMeshes`: AuthoringReadWrite collection
- runtime matrix: InternalOnly または必要時のみ RuntimeReadOnly

`runtime*` prefix だけで機械的公開しない。schema で明示する。

## collection / nested struct

対象例:

```text
MeshRendererComponent.subMeshes
SubMeshMaterial
CollisionComponent.shapes
CameraControllerComponent.follow/lookAt/shake
BillboardComponent.axes
```

要件:

- nested struct wrapper または value snapshot + commit API。
- list element の取得、更新、追加、削除、並べ替え。
- collection mutation は component の存在確認と index 検証を行う。
- 大きな collection は毎 access で全 JSON serialize しない。
- authoring Inspector と gameplay API で使う経路を分離してもよい。

## generator tool

推奨:

```text
Engine/Tools/NEM.ManagedBindingGenerator/
```

.NET console tool または repository 内で再現可能な generator を使う。

必要 command:

```text
--generate
--verify
```

`--verify` は generated file が schema と一致しない場合に non-zero を返す。CI / build 前確認に使う。

## component 対応範囲

`07_csharp_gameplay_api.md` に列挙した component をすべて schema へ入れる。現時点で `Engine/Core/World/Components` にある component を棚卸しし、除外 field は理由を schema comment または隣接ドキュメントへ書く。

## custom method

単純 property accessor で表現できない操作は手書き extension partial にする。

例:

```text
AudioSource.Play / Stop
SkinnedAnimation.Play / CrossFade / Stop
CameraController.RequestShake
Prefab.Instantiate
SceneManager.LoadAdditive
```

生成 wrapper と手書き partial を分ける。

## project integration

- generator 実行を build step に組み込む。
- generated output が stale なら build failure または自動生成。
- `NEMEngine.vcxproj` と `.filters` を更新する。
- generated `.cs` は ScriptCore project に含める。
- source monitor は generated output を GameScripts source change と誤認しない。

## 回帰テスト

- schema から生成し直して diff が出ない。
- `--verify` が stale output を検出する。
- 各 component の `Has`, `Add`, `Remove`, property get/set が動作する。
- writable 除外した runtime field が C# から書けない。
- SpriteRenderer の通常 property access に managed allocation がない。
- MeshRenderer subMeshes の list 操作が round-trip する。

## 完了チェックリスト

- [ ] binding schema が一つの source of truth。
- [ ] native dispatch と C# wrapper が生成される。
- [ ] hot path が string lookup に依存しない。
- [ ] 全 native component の field を公開 / 非公開理由付きで棚卸しした。
- [ ] custom operation は partial 手書き層に分離した。
- [ ] `--verify` がある。

<!-- END 09_component_binding_codegen.md -->


<!-- BEGIN 07_csharp_gameplay_api.md -->

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

<!-- END 07_csharp_gameplay_api.md -->


<!-- BEGIN 12_editor_scripting_tooling.md -->

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

<!-- END 12_editor_scripting_tooling.md -->


<!-- BEGIN 13_full_game_readiness_scope.md -->

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

# 13. 「ちゃんとしたゲームを作れる」状態まで統合して完成判定する

## 目的

最小 API の途中状態で止めず、今回指定された scripting 基盤を統合し、残件を可視化して 0 件にする。このファイルは最後に実行する統合タスク。

## 明示的な見送り

今回の完了条件から除外するのは次の 2 件だけ。

```text
Play 中 DLL hot reload
native physics 未整備のため、新規 Rigidbody / Raycast / Overlap API
```

既存 collision callback と collision component binding は壊さない。

## 統合対象

### Runtime 安全性

- World handle generation
- Entity handle generation
- Managed instance handle generation
- ABI version / size / capability
- managed exception containment
- invocation context RAII
- deferred structural command
- hostfxr 標準探索
- ALC unload diagnostics

### Lifecycle

- 全 instance create pass
- inactive hierarchy 対応 Awake pass
- enable transition pass
- scene event pass
- Start pass
- Update / flush
- LateUpdate / flush
- FixedUpdate / flush
- deterministic execution order

### Serialization / Inspector

- stable script type ID
- stable script slot ID
- stable field ID
- FormerlySerializedAs
- 全指定 field type
- 全指定 Inspector attribute
- dynamic buffer
- unresolved field retention
- runtime value inspector
- Undo / Redo
- Missing Script

### Gameplay API

- Entity create / destroy / hierarchy / component access
- Transform 全 API
- native component generated wrapper
- AssetRef
- EntityRef
- ScriptRef
- Prefab Instantiate
- Scene additive load / unload
- Scene event
- Input raw API
- Input action map / rebind / profile
- multi-gamepad
- Time
- TimeScaleComponent
- Timer
- Coroutine
- AudioSource control
- Animation control
- Camera / light / renderer control
- application event のうち native 起点が存在するもの
- logging と structured diagnostic

### Editor

- async build
- staging / shadow / last-known-good
- compiler error list
- IDE jump
- script template
- manifest validation
- managed profiler
- exception entity selection
- pending reload 表示

## 全 component 棚卸し

`Engine/Core/World/Components` の各 component を一覧化し、generated binding schema に対して次の表を作る。

```text
component
field
classification: AuthoringReadWrite / RuntimeReadOnly / RuntimeCommand / InternalOnly
C# API 名
Inspector 表示有無
非公開理由
```

分類漏れを 0 にする。新 component 追加時に generator verify が漏れを検出できるようにする。

## Vertical Slice テストゲーム

C# scripting だけで次の動作を作る Sandbox scene を追加する。テスト専用でもよい。

### 必須シナリオ

1. Player script が Input Action `Move` で Transform を移動。
2. camera controller parameter を C# から調整。
3. key 入力で prefab enemy を instantiate。
4. enemy script が coroutine と timer を使う。
5. AudioSource を C# から Play / Stop。
6. SpriteRenderer または MeshRenderer の色 / visible を変更。
7. SkinnedAnimation を Play / CrossFade。
8. additive scene を load / unload し、sceneLoaded event をログ。
9. TimeScaleComponent を切り替え、scaled / unscaled timer を比較。
10. EntityRef と ScriptRef が scene reload / prefab instantiate 後に正しく解決。
11. intentional exception script が faulted になり、他 script と Editor は継続。
12. inactive parent の子 script が active 化まで Awake されない。

## Stress テスト

### Handle

```text
Play / Stop 500 回
Entity create / destroy 100,000 回
managed script attach / detach 100,000 回
古い handle access
```

### Reload

```text
Edit mode reload 100 回
syntax error -> fallback -> fix -> reload
static event leak injection -> warning
obj 更新 -> reload storm なし
```

### Serialization

```text
長い string
大きな List
nested list / reference
rename migration
missing script
missing asset
prefab internal ref
scene ref
```

### Performance

計測する。

```text
10,000 entity の isAlive query
1,000 script Update
component property get/set
Transform move
input action query
coroutine 1,000 件
```

目標値は環境依存なので、baseline と変更後を記録する。機能追加で明確な退行がないことを確認する。

## Build matrix

最低限:

```text
NEMEngine Debug x64
NEMEngine Develop x64
NEMEngine Release x64
NEM.ScriptCore Debug / matching profile
GameScripts build
binding generator --verify
script manifest validation
```

実行環境がなく一部 build 不可能な場合は、コマンド、失敗理由、未検証範囲を報告する。

## Documentation

repository 内に developer document を追加する。

```text
C# script の作り方
constructor に書いてはいけない処理
lifecycle 順序
Play 中 hot reload 非対応
serialized field 対応型
FormerlySerializedAs の使い方
AssetRef / EntityRef / ScriptRef
deferred command と flush 前 Entity の扱い
coroutine lifetime
component binding schema の追加方法
generator 実行方法
hostfxr / .NET runtime 配布要件
```

## 未完了検出

最後に repository 全体を検索する。

```text
TODO
FIXME
stub
not implemented
throw new NotImplementedException
空の placeholder
旧 `ManagedNativeEntity.world` pointer
旧 int managed handle
固定長 serialized metadata buffer
filename stem script type resolution
Play 中 reload
```

今回の scope に属するものが残っていれば完了としない。

## 完了報告

次を必ず出す。

```text
全実装項目一覧
全変更ファイル一覧
build matrix 結果
vertical slice 結果
stress test 結果
performance baseline 比較
migration 方針
明示的な非対象 2 件
未完了件数: 0
```

## 完了チェックリスト

- [ ] 02-12 の各指示書の完了条件を再確認した。
- [ ] component binding 棚卸しの分類漏れがない。
- [ ] Vertical Slice が C# scripting で動作する。
- [ ] Stress test を実施した。
- [ ] Build matrix を確認した。
- [ ] developer documentation を追加した。
- [ ] 今回 scope の TODO / stub が残っていない。
- [ ] 未完了件数を 0 と報告できる。

<!-- END 13_full_game_readiness_scope.md -->
