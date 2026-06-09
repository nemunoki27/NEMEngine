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
