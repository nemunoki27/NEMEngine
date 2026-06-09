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
