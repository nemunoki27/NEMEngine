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
