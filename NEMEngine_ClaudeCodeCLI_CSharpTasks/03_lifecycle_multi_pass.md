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
