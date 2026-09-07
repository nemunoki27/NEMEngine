# NEMEngine実装調査・開発引き継ぎ

調査日: 2026-09-05

対象は`C:/Users/k023g/school/NEMProjects/NEMEngine`の現行ソース、Premake、配布ツール、テンプレート、Sandbox、既存テスト、生成済みSDKの構成。今後の実装で入口・所有者・データの流れ・回帰確認先を見失わないための記録。

主要経路は実装を追跡して確認した。全ソースの全行レビュー、全組み合わせの動作保証、性能評価、GPU実機検証を完了したという意味ではない。GJ4のチーム開発環境はユーザーから共有された背景情報であり、今回そのディレクトリへの調査・変更は行っていない。

## 開発時に保持する方針

- 機能を追加するときは既存機能を保存する。変更箇所だけでなく、呼び出し元、保存・復元、C#公開、Edit/Play切り替え、描画・SDK側の利用箇所まで影響を追う。
- 既存の機能や分岐を、今回の用途で使わないという理由で削除しない。仕様の変更・廃止が必要な場合は影響を先に説明する。
- 実装前に周辺コードと親ディレクトリの`AGENTS.md`、`templateClass.h/.cpp`を確認する。命名、タブ、include順、空行、簡潔な日本語コメントを合わせる。
- 変更後は実施した検証と未検証を区別する。ビルド成功だけで表示・ライフサイクル・保存互換性まで成功したとしない。
- Git履歴を変更する操作、SDK公開・更新の実行は今回行っていない。スクリプト内部のGit操作も対象として扱う。
- Premakeはファイル追加・移動・削除や構成変更など、プロジェクト再生成が必要な場合に限る。

## 1. プロジェクト構成とビルド境界

現在の起動構成は`NEM_RunEditor()`を公開する旧DLL構成ではない。

```text
Project/Engine/Core       → NEMCore.lib
                            ├─ Project/Engine/Editor → NEMEditor.exe
                            ├─ Project/Engine/Public → NEMRuntime.dll
                            │                          └─ Sandbox.exe / ゲームexe
                            ├─ Project/Tools/NEM.BuildTool → NEMBuildTool.exe
                            └─ Project/Tests → NEMTests.exe
Project/Engine/Managed    → NEM.ScriptCore / CodeGen / Analyzers / MetaSync
Project/Sandbox          → GameAssets + GameScripts + 起動用C++
Templates/GameProject    → SDK利用ゲームの構成テンプレート
Tools                    → SDK作成・公開、ゲーム生成、製品ビルド、依存DLL配置
Generated                → ビルド成果物・外部ライブラリ生成物・SDK
```

根拠: [premakeNemengine.lua](../Premake/premakeNemengine.lua)、[公開ヘッダー](../Project/Engine/Public/NEMEngineRuntime.h)、[Sandbox入口](../Project/Sandbox/main.cpp)、[Editor入口](../Project/Engine/Editor/main.cpp)。

`NEMRuntime`は公開C ABIの`NEM_RunGame()`から`GameApplication`を起動する。`NEMEditor`は`RunEditorApplication()`から`EngineApplication`を起動し、共通Coreへ静的リンクする。SDK利用ゲームは公開ヘッダーとimport libraryを使い、Engine/Coreのヘッダーには依存しない。

`Project/GameProjects/<container>/<app>`は取り込んだゲームをエンジン開発Solutionからビルドする経路。`gameProjects.lua`が`GameAssets`の存在で発見する。SDK利用側の独立ゲームSolutionとは別の経路。

## 2. Premake・SDK・製品配布

### ソース開発のPremake

[premake5.lua](../Premake/premake5.lua)はDebug/Develop/Release、x64、開始プロジェクトNEMEditor。共通C++設定はC++20、静的CRT、UTF-8。Developはネイティブ最適化ありで、C#側のDevelopはデバッグを考慮して最適化を無効にしている。

[generate_vs2026.bat](../Premake/generate_vs2026.bat)はBinding生成・検証、外部ライブラリ構成、Premake、C#プロジェクトのSolution追加、ネイティブ／Managedデバッガ設定の補正を行う。単に`.vcxproj`を出力するだけではない。外部ライブラリにはDevelop→Releaseのconfigmapがある。

実行が必要な場合は`Project`を作業ディレクトリとして`../Premake/generate_vs2026.bat`を呼ぶ。生成済み`.vcxproj/.filters/.slnx`の手編集を実装の正にしない。

HLSLはVisual Studioの通常コンパイル対象ではなく、表示用のNone項目。開発時はエンジン内DXC、製品はShader Cookの経路を使う。

### SDKの作成と利用

[PackageEngineSDK.ps1](../Tools/PackageEngineSDK.ps1)は、既定で3構成のSandboxとNEMEditorをSolution経由でビルドする。Sandboxを入口にRuntimeとManagedツールチェーンを揃え、取り込んだゲームの同時C#ビルドによる共通出力先の競合を避けている。

主なSDK出力:

| SDK内の配置 | 内容 |
|---|---|
| `Include` | 公開C ABIヘッダー |
| `Bin/<構成>` | NEMRuntime.dll、import library |
| `Runtime/<構成>` | ゲーム実行用DLL、Managed、依存マニフェスト |
| `Editor/<構成>` | NEMEditor.exe、実行依存DLL、Managed |
| `Managed/Ref` | C#ビルド参照用ScriptCore |
| `Managed/Analyzers`、`Managed/Tools` | CodeGen、Analyzers、MetaSync |
| `Engine/Assets` | エンジン共通アセット。Editor用も別途同期される |
| `Premake`、`GameProject`、`Tools` | SDK利用側ヘルパー、テンプレート同期、BuildGame |

Managed配置はディレクトリ内容を同期し、ScriptCoreのSHA256一致と二重の構成ディレクトリがないことを検査する。NEMEditorのネイティブPDBは配布から除外し、C#デバッグ用PDBは保持する。

[DeployRuntimeDependencies.ps1](../Tools/DeployRuntimeDependencies.ps1)が実行依存DLL配置を共通化。[PublishSDK.ps1](../Tools/PublishSDK.ps1)は公開用Gitリポジトリの履歴・indexを操作してcommit/pushするため、調査用には実行しない。

新規ゲームは`NewGame.ps1`／`CreateGameProject.bat`と`Templates/GameProject`を起点にする。SDKは通常`External/NEMEngine`、設定・環境変数による別ルート指定も可能。`nem_game.lua`がRuntimeリンク、MetaSync、GameScriptsビルド、実行時コピーを設定する。

ゲーム側F5の起動先は`patch_vcxproj_user_debugger.ps1`で補正する。テンプレートではDebug/DevelopでSDKのNEMEditor、Releaseではゲーム自身が起動する。作業ディレクトリとManagedデバッガ種別もこの経路の一部。

`Templates/GameProject/Tools/UpdateSdk.ps1`はジャンクション参照なら取得を省略し、Git参照ならSDKを更新する。その後、修復・サポートファイル同期、Premake再生成、3構成のリビルドを行う。単なるDLLコピーではない。

### 製品ビルド

[GameBuildService.cpp](../Project/Engine/Editor/Build/GameBuildService.cpp)が起動Scene、参照Asset、Shader include、モデル付属ファイル、ExternalActors、Packageを収集してManifestを作る。[BuildGame.ps1](../Tools/BuildGame.ps1)がReleaseビルド、Shader Cook、ステージング配置、ハッシュ記録、製品ディレクトリ交換を担当する。

製品はCook済みDXILを使用し、配置したMaterialからShader Graphソース参照を外す。HLSL/HLSLI/Shader Graphソースを出荷対象から除き、`.nemBuildManifest.json`と`.nemCookManifest.json`を作る。現在のSDKで不足する経路は後述の調査事項Aを参照。

## 3. フレームワークとWorldの流れ

[EngineFramework.cpp](../Project/Engine/Core/Runtime/Framework/EngineFramework.cpp)がウィンドウメッセージ、時間、入力、Application更新、描画提出、Presentを制御する。

```text
Framework::Run
  → Init: GraphicsCore / Input / Application
  → フレーム: メッセージ → リサイズ → 時間・入力 → Application::Tick
           → BeginRenderFrame → Application::Render → メイン描画提出
           → EditorのPlatform Window描画 → Present
  → Finalize: Application → Input → GraphicsCore → Logger
```

ApplicationはAssetDatabase、SceneInstanceManager、WorldManager、SystemScheduler、Managed runtime、RenderPipelineRunnerを接続する。Editorとゲームで起動・終了・Scene設定・UIが異なり、共通System登録は[RuntimeSystemRegistration.cpp](../Project/Engine/Core/Runtime/Application/RuntimeSystemRegistration.cpp)へまとまっている。

EditorのPlay:

1. スクリプトビルド／再読み込みの完了を待つ。失敗時はPlayへ移らない。
2. 通常Scene編集中は編集Sceneを保存する。
3. Edit側Scene群をSnapshotへ直列化し、新しいRuntime Worldへ復元する。
4. RuntimeWorldBakerを接続して初回同期し、ManagedWorldRegistryへ登録、Play時間を初期化する。
5. `RefreshActiveWorldContext()`が更新・描画・Editor・WorldCommand用サービスの参照を現在のWorldへ揃える。
6. StopではSystemを切り離し、RenderFeature overrideとBaker、Managed World handleを解除してPlay Worldを破棄する。

根拠: [EngineApplicationPlay.cpp](../Project/Engine/Editor/Runtime/Application/EngineApplicationPlay.cpp)。Pause中はWorld更新を止め、FrameStep時に進める。開始直後の初回描画の重さを次フレームdeltaTimeへ混入させない処理がある。`GameApplication`もEdit相当の初期読み込みからPlay Worldを作る。

### System更新順序

[SystemScheduler.cpp](../Project/Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.cpp)はSystemの並列ジョブスケジューラではなく、order順の逐次実行。FixedUpdateの各サブステップ、全Update後、全LateUpdate後にWorldCommandをFlushし、Scene変更時にはHeaderの更新とLifecycle再同期を行う。最後に遅延Entity破棄を確定する。既定fixedDeltaTimeは1/60秒、最大32サブステップ。

登録順は次のとおり。各Systemが実装するFixed/Update/Lateだけがそのフェーズで動くため、この列をそのまま単一Updateの実行列とは解釈しない。

`Hierarchy → RenderFeatureProfile同期 → UIInput → Behavior → AnimationPlayer → Physics → AudioSource → CameraController → CameraShake → Transform → Particle → Collision → Flipbook → UVTransform → SkinnedAnimation → JointAttachment → UICanvas`

Profileの同期はC#初期化より先、UI入力はC# Updateより先、JointAttachmentはスケルトン更新後、Canvas行列はTransform更新後という依存がある。

## 4. C++とC#の連携

入口: [ManagedScriptRuntime.cpp](../Project/Engine/Core/Scripting/Managed/ManagedScriptRuntime.cpp)、[HostBridge.cs](../Project/Engine/Managed/NEM.ScriptCore/Runtime/HostBridge.cs)、[NativeApi.cs](../Project/Engine/Managed/NEM.ScriptCore/Runtime/NativeApi.cs)。`DotnetHostResolver`でhostfxrを解決し、.NET 10のScriptCoreをロードする。

```text
C#ゲームScript → ScriptBehaviour / Entity / 各Component
             → NativeApiのCdecl関数ポインタ
             → ManagedScriptBridge各実装・生成Binding → ECS/System

BehaviorSystem → ManagedBehavior → ManagedScriptRuntime
             → HostBridgeの公開関数 → C# Lifecycle
```

現行ABI versionはC++/C#とも49。初期化時はversion、構造体サイズ、capabilityを検査し、不一致なら関数テーブルを使用しない。

生成元は`Bindings/ComponentManifest.json`と`Bindings/ManagedNativeApi.json`。`Project/Tools/NEM.ComponentBindingGen`が固定Component ID、C++登録、C++ Binding、C# wrapper、双方のABIテーブルを生成する。現行は61種類のComponent登録、25種類のGeneratedBinding、158コールバック。InternalOnlyやHandwrittenFacadeもあり、全Componentが自動公開されるわけではない。

Script型・SerializeFieldの永続IDは`.cs.meta`が正。MetaSyncが同期し、CodeGenがManifest/schemaを埋め込み、AnalyzerがコンストラクタでのEngine API・イベント・非同期処理等の副作用を診断する。名前変更時に型GUID、field GUID、scriptSlotIdを失うと既存Scene/Prefabとの対応が壊れる。

World、Entity、Script instance、ストレージのハンドルは世代で失効を判定する。`ManagedWorldRegistry`でWorldが破棄された後の古いC#参照を拒否する。Asset GUID、Entity UUID、localFileID、scriptSlotId、実行時instance handleは用途が違う。

### Lifecycleと構造変更

[BehaviorSystem.cpp](../Project/Engine/Core/World/Systems/Behavior/BehaviorSystem.cpp)は全Script実体の同期、保留参照解決、実行順整理、Awake、Active遷移、Sceneイベント、Startの順を管理する。

- Awakeは未実行かつ対象Entityがactiveなときに実行する。
- OnEnable/OnDisableは状態遷移時のみ。Startは一度だけで再有効化時に再実行しない。
- SceneLoaded/Unloadedの通知はAwake/OnEnable後、Start前にpumpする。
- LateUpdateは同フレームにUpdateへ参加したScriptに限定する。
- 破棄時のOnDisableは有効だったもの、OnDestroyはAwake済みのものを対象にする。
- Timer/Coroutine/Eventの所有者破棄、Play停止、Assembly終了時の後始末を持つ。

通常のC# Component追加・削除、Sceneロード・UnloadはWorldCommandへ積まれる。一方、`ManagedScriptBridgeGameplay.cpp::InstantiatePrefabCallback`は即時生成し、全Scriptの生成と参照解決、Awake/OnEnableまでを返却前に同期する。Startは呼び出し元callbackの終了後に通常同期する。すべてのAPIを一律の即時／遅延操作へ揃えてはいけない。

### スクリプト再読み込み

[ManagedScriptBuildService.cpp](../Project/Engine/Core/Scripting/Managed/ManagedScriptBuildService.cpp)は変更監視→debounce→MetaSync→Staging build→Manifest生成→Shadow copy→Editでreloadを行う。失敗時は現行Assembly維持またはLastKnownGoodへ復帰する。Play中にはreloadを適用しない。

HostBridgeはcollectible AssemblyLoadContextを使用する。`ScriptRuntimeLifetime`で購読や非同期処理を解放し、古い型・instanceへの参照を消し、限定回数のGCでunload結果を診断する。DLLを直接上書きする変更や、static eventへの解除されない購読はこの設計に影響する。

## 5. アセット管理・参照・保存

[AssetDatabase](../Project/Engine/Core/Assets/Database/AssetDatabase.h)はGPUリソース所有者ではなく、GUID、論理パス、種別、Importer設定、依存先、逆引き参照、診断を保持する索引。`.meta`破損、GUID重複、孤立meta、参照欠損、種別違いを診断する。テクスチャやMeshの実体はそれぞれの読み込み／GPU管理サービスが持つ。

AssetIDは128bitのAssetGUID。Scene等のUUIDは64bitで、文字列長・解析関数も別。永続Asset参照はGUIDで保存し、実パスはDatabase/RuntimePaths経由で解決する。

[RuntimePaths.cpp](../Project/Engine/Core/Runtime/Paths/RuntimePaths.cpp)が`.nemproject`、ソース開発／SDK／製品配置、`NEMENGINE_ROOT`、各設定・Library・Savedの位置を解決する。仮想パスは`engine://`、`game://`、`library://`、`user://`、`package://`。PackageResolverはmanifestとlockを扱い、パッケージをAsset走査ルートへ加える。

[AssetWatchService.cpp](../Project/Engine/Core/Assets/Watch/AssetWatchService.cpp)はファイル変更を集約してTexture、Mesh、Material/Shader/Pipelineの再読み込みへ振り分ける。追加・削除等はDB再構築、meta変更はImporter設定反映を含む。`RenderPipelineRunner::ReloadAsset`は依存元MaterialやGraphを含めて扱う。拡張子を追加するときは型判定、依存抽出、Importer、監視、ProjectPanel、製品収集を一緒に確認する。

`AssetDatabase::RebuildMeta()`は純粋な読取検査ではない。meta生成とフォントatlas参照補正を含む。PackageResolverもlockを更新する。これらを呼ぶ診断ツールを、無変更の監査として無条件に実行しない。

## 6. ECSとComponentの所有権

入口: [ECSWorld.h](../Project/Engine/Core/World/ECS/World/ECSWorld.h)、[EntityChunk.h](../Project/Engine/Core/World/ECS/Entity/EntityChunk.h)、[ComponentTypeRegistry.h](../Project/Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h)、[ECSStorage.h](../Project/Engine/Core/World/ECS/Storage/ECSStorage.h)。

- Entityはindexとgeneration。永続UUIDはWorldのEntityRecordで管理する。
- 同じComponent集合をArchetypeでまとめ、16KiB・64byte alignmentのChunk内にEntity列とComponent列を置く。Component種別上限256、Chunk Entity数上限1024。
- Component追加・削除ではArchetype間の移動が起きる。削除は末尾行とのswapを使うため、Componentのポインタやspanを構造変更をまたいで保持しない。
- ForEachは要求Signatureのmatch planを保持し、Archetype追加で無効化する。DynamicBufferやTagは通常の値型ForEachと扱いが異なる。
- 可変長のScriptEntry、SubMeshMaterial、CollisionShape等はDynamicBufferを使う。重い実行データにはWorld所属の世代付きPoolも使う。
- Registryは構築／破棄／移動、Storage初期化、OnAdded/OnRemoved、SerializeECS/DeserializeECSなどのhookを束ねる。
- `kSerializable=false`は「消してよいデータ」を意味しない。所有ComponentのSerializeECSから保存するBufferと、保存しない実行状態の両方がある。ScriptEntryやSubMeshMaterialが例。
- Authoring/Runtime/Bothのdomain、enableable状態、Render/Lighting変更チャネルを持つ。

`WorldManager`はAuthoring WorldとRuntime Worldを分ける。ただし非保存の実行補助ComponentがEditプレビューで使われる例もあるため、名称だけからWorld domainを推測しない。

[RuntimeWorldBaker.cpp](../Project/Engine/Core/World/ECS/Baking/RuntimeWorldBaker.cpp)の現行Bake対象はMeshRendererで、サブメッシュをモデルの安定IDへ同期する。すべてのRuntime ComponentをBaker一箇所で作る構造ではなく、各ComponentのOnAdded/Storage hookも必須の構築経路。

描画・ライティング抽出はRevisionに基づくキャッシュがある。値を書き換えるだけでなく`MarkComponentModified`、Transformのdirty通知、changeChannels/transformChannelsまで確認する。値は変わったのに見た目が変わらない場合の重要な追跡先。

## 7. Scene・Prefab

[SceneSystem](../Project/Engine/Core/World/Scene/Runtime/SceneSystem.h)が保存・復元、[SceneInstanceManager](../Project/Engine/Core/World/Scene/Runtime/SceneInstanceManager.h)がロード済みScene群、active Scene、SubScene、revisionを管理する。

現行Scene形式はSchemaVersion 3。通常Entityと薄い`PrefabInstances`を分ける。`.nemproject`の`sceneStorage`に応じてMonolithicまたはExternalActors形式を選ぶ。ExternalActorsはScene参照GUID下のEntity単位ファイルで、ロード・保存・製品収集にそれぞれ処理がある。Sandboxの現在値はMonolithic。

Editorの通常保存は`CloneForSerialization`で独立World、Scene群、DBをSnapshot化してワーカーへ渡す。保存完了時は取得時のdirty revisionを使い、保存中の追加編集を保存済み扱いにしない。Play開始や終了時は保存Jobの完了を待つ。

Prefabの主要所有者:

| 操作 | 追跡先 |
|---|---|
| 元Prefab保存・生成 | [PrefabSystem.cpp](../Project/Engine/Core/World/Prefab/Runtime/PrefabSystem.cpp) |
| 差分抽出・再構築・伝播 | [PrefabOverrideUtility.cpp](../Project/Engine/Core/World/Prefab/Override/PrefabOverrideUtility.cpp) |
| Scene/Prefab参照空間の変換 | [PrefabReferenceRemapper.cpp](../Project/Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.cpp) |
| 隔離編集・in-context・編集段階 | [EngineApplicationPrefab.cpp](../Project/Engine/Editor/Runtime/Application/EngineApplicationPrefab.cpp) |
| Editor生成・Unpack・Undo | `Editor/Commands/Entity` |

Prefabは元Asset、Prefab内localFileID、配置先Scene localFileID、instance ID、Entity UUID、nestedSlotIDを区別する。生成時は全Entityと対応表を先に作り、Component値と参照を書き戻し、Meshサブメッシュ、親子リンク、ネストを復元する。

`SceneObject`の保存値を読み込んだ後、配置先固有のlocalFileID/sourceAsset/sceneInstanceIDを設定する順序がある。タグ・activeSelf等を復元対象から落とさない。元Prefabの再構築では安定UUID、ネストの差分・削除slotも保持する。Unpackは最外側のみと完全解除の2方式。

Prefab変更の最低確認範囲は、新規生成、保存・再読込、内部C#参照、ネスト、追加／削除Component・Entity、Apply/Revert/伝播、Unpack、Undo/Redo、Play生成。元ファイルだけを見て保存成功と判断しない。

## 8. グラフィックス

### 所有者と描画経路

[RenderingCore.cpp](../Project/Engine/Core/Rendering/Core/RenderingCore.cpp)のGraphicsCoreはWindow/context、GraphicsPlatform、SwapChain、Descriptor、Texture/Buffer upload serviceを束ねる。GraphicsPlatformはDX12 Device、Command、Queue、FramePresenter、機能検出、開発時DXCを管理する。

[RenderPipelineRunner.cpp](../Project/Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.cpp)はWorldからの抽出、View解決、Light集合、Asset/PSO、バックエンド、Raytracing Scene、固定RenderPathを接続する。

```text
ECS + 変更Revision
  → RenderExtractorRegistry → RenderSceneBatch
  → View/Scene/Camera/Layerに応じた選別・Pass bucket
  → Material解決 → Pipeline variant / PSO / Reflection binding
  → Mesh・Primitive・Sprite・Text・Line・Particle backend
  → View別RenderTarget → 表示用View → BackBuffer / Editor UI
```

GameView、SceneViewはView別の色・深度・ライト等を保持する。ツールプレビューには専用Backendとフレーム内資源Poolがあり、複数previewで同じGPU Bufferを上書きしない構成。片方のViewだけの修正や、プレビューとの無条件共有は避ける。

### 固定描画パスと拡張

[DeferredRenderPath.cpp](../Project/Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.cpp)の実装順:

`Clear → DepthPrepass → Opaque/GBuffer → Lighting → InvertedHullOutline → Transparent → RuntimeScreenSpaceOutline → PostProcessUI → Editor選択Outline → BlitToView → ScreenUI → DebugOverlay → EditorOverlay`

ここへBeforeLighting、AfterLighting、BeforeTransparent、AfterTransparent、AfterPostProcessUI、BeforeBlitの6か所でRenderFeatureを挿入する。ヘッダーや未使用includeに存在するPass名だけを実際の実行順とみなさない。

[RenderFeatureProfile.h](../Project/Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h)が現行の統合設定。ComputeとDispatchRaysのPass、階層Group、対象Layer/Renderer/Phaseの選択、分離描画・合成、名前付き出力、前フレームhistory、解像度調整、parameter/texture/sampler overrideを持つ。旧PostProcessStackの知識だけで編集箇所を決めない。共通カラー処理には露出、Color Grading、Filmic設定がある。

### 機能と関連ファイル

| 機能 | 主な経路 |
|---|---|
| Mesh描画、Mesh Shader、通常Vertex経路 | `Renderer/Backends/Builtin/Mesh`、`MeshGPUResourceManager`、`MeshletBuilder` |
| スキニング・LOD・カリング | Mesh backend、`Meshes/Animation`、Mesh/Culling HLSL、GraphicsFeatureController |
| PBR・GBuffer・Light・Skybox/IBL | `Renderer/Lighting`、`Assets/Shaders/Builtin/Mesh/Common`、LightingPass |
| Inline RayTracing / DispatchRays | `RaytracingSceneBuilder`、BLAS/TLAS、RayTracingExecutor、RaytracingPipelineState |
| Material・Shader・Pipeline | `Rendering/Assets`、MaterialResolver、PipelineStateCache、Reflection/RootBinder |
| Shader Graph | NodeRegistry → IR/Compiler → ArtifactCache → Materialの複数Pass |
| Texture import / reload | TextureImportSettings、TextureUploadService、RuntimeTextureResolver |
| Particle・Trail | World/Systems/Effect、Particle各Registry、ParticleRenderBackend、TrailDataBuilder |
| 文字・図形・線・Sprite | 各Extractor/Backend、MSDF生成、UIRuntimeService |
| GPU計測・障害診断 | GPUFrameProfiler、PIX event scope、DxDredDiagnostics |

Shader GraphはOpaque/Transparent/Depth/Picking/Vertex/Mesh/RayTracing/Compute向け出力を持ち、Materialパラメータの安定IDを付ける。生成物はLibraryへ置く。表面計算の変更では通常描画だけでなくDepth/Picking/RayTracingの整合も確認する。

GraphicsFeatureControllerは検出supportとユーザーpreferencesから実行時機能を決める。Mesh Shader、Inline RayTracing、DispatchRaysを独立して扱い、カリング・LODも個別設定がある。設定がtrueであることとGPUで実行可能であることを混同しない。

フレーム間のGPU資源再利用はFrameContextとFenceに従う。BufferUploadはBatch提出、Textureは非同期decode/uploadとFinalizeを使う。リサイズ、資源差し替え、複数View・preview、終了時に資源寿命を確認する。Editor SwapChainはSDRを強制し、製品側にはDisplay出力設定がある。

## 9. エディター

[EditorManager.cpp](../Project/Engine/Editor/Core/EditorManager.cpp)がパネル・ツール・コマンド・ImGuiフレームを接続し、ApplicationがScene/Play/Prefabの実行状態を所有する。UIからの操作要求と、World切り替え・保存の実行箇所は分かれている。

標準パネルはMenuBar、Toolbar、Hierarchy、Inspector、Console、Tool、Project、GameView、SceneView。Project/Inspectorの追加インスタンス生成もある。レイアウト保存・復元とパネルID/表示順は互換性の対象。

Componentの表示・追加メニューは`BuiltinComponentEditorRegistration`→ComponentEditorRegistry→Drawer。ScriptComponentは複数Scriptを持つ内部コンテナとして特別扱い。Asset Inspector、AssetActionRegistry、ProjectAssetIndex/ThumbnailCacheは別登録。

編集操作は`IEditorCommand`とcommandHistoryを用い、生成・削除・複製・reparent・serialized field変更等をUndo/Redoへ接続する。Play中の変更をAuthoringへ適用する専用Commandもあり、自動的にEdit Worldへ書き戻す設計ではない。

ツールにはMaterial、Shader Graph、RenderFeatureProfile、ParticleEffect、AnimationClip、SceneComposition、Tag、Performance、ScriptExecutionOrder等がある。`BuiltinEditorTools.cpp`ではInputDeviceTool登録が不安定を理由にコメントアウトされている。ファイルがあることと標準UIから使用可能であることを区別する。

SceneViewのGizmo・ピック、階層選択、複数選択、複製、Copy/Paste、Prefab編集状態、UI入力の消費、dirty revisionは相互に関係する。独自にWorldへ直書きするUIを足す前にCommand/Drawerの既存経路を確認する。

## 10. Sandboxと周辺システム

[Sandbox.nemproject](../Project/Sandbox/Sandbox.nemproject)はGameAssets/Packages/ProjectSettingsとScene保存方式を定義する。C++mainはNEM_RunGameだけを呼ぶ。NEMEditorのソース開発時debugdirはSandboxで、C#ゲームコードは`Scripts/GameScripts.csproj`から`GameAssets/**/*.cs`と`.cs.meta`を取り込む。

現行のSceneはStage1、Sponza、ManyEntity、DebugPostProcess。JSON上ではStage1に通常Entity1件とPrefab instance4件、ManyEntityに通常Entity426件がある。これは保存データの数であり、Prefab展開後のEntity数や描画性能の測定値ではない。

ゲームScriptには`Player/Player.cs`、`Player/Player3D.cs`、Test/TestB、RenderFeature制御・Iris transition例がある。2D PlayerはA/D、Wジャンプ、Rigidbody2D、入力猶予とコヨーテタイムを使う。2Dスクリーン座標の上向きは負Y。3D版は別クラス・別操作系であり、2Dの数値・符号をそのまま移さない。

周辺機能も以下の経路で把握した:

- PhysicsSystemがFixedで剛体を積分し、CollisionSystemが形状判定・応答と接触通知を担当する。2D/3D、回転固定、Trigger、typeMask/Collision Matrixを別条件として確認する。
- TransformSystemは親子階層のdirty伝播とWorld行列を更新し、描画・Lightingの消費者へ変更を通知する。
- AnimationPlayerはClip/Curve/PropertyRegistryを経由した値の更新、SkinnedAnimationは骨格、JointAttachmentは骨への接続を担当する。
- AudioSourceSystemはWorld退出時のStopAllを持ち、AudioSystemはXAudio2とMedia Foundationを使う。
- ParticleSystemはComponent設定・コマンド、World所属runtime、Emitter/Module/Shape、Trail、描画Backendが分かれる。EditプレビューとPlay、Local/World/Custom空間、停止後の残存粒子を考慮する。
- Canvas、UISelectable、画像・文字Button、UIProgressとUIRuntimeServiceが画面行列と入力を共有する。Editorのゲーム入力ブロックと保存前の見た目復元も確認対象。

## 調査で見つかった不整合・設計上の注意

### A. SDKだけの環境で製品ビルドが完結しない経路

初回調査でソース上の不整合を確認。その後、ユーザーがGJ4で`NEMBuildTool project was not found`を再現した。

修正前の`GameBuildService.cpp::WriteManifest`は`buildToolProject`をEngineProjectRoot配下の`Tools/NEM.BuildTool/NEMBuildTool.vcxproj`、exeをエンジン側Generated/Output配下に設定していた。`Tools/BuildGame.ps1`はこのvcxprojの存在を必須にしてMSBuildしていた。一方、`PackageEngineSDK.ps1`はNEMBuildToolをビルド・同梱せず、初回調査時のGenerated/SDK/ToolsにもBuildGame.ps1しかなかった。

2026-09-05の追加実装で修正。SDK作成時にReleaseのNEMBuildToolをビルドし、`Tools/NEMBuildTool/NEMBuildTool.exe`とDXC等の依存DLLを同梱する。`Include/NEMEngineRuntime.h`があるSDK環境では、WriteManifestが`buildToolProject`を空にし、同梱exeを指定する。BuildGame.ps1は空の場合にツールのMSBuildを省略する。ソース開発環境のプロジェクト指定・ビルド経路は維持した。

実行検証で、Shader種別のHLSL/HLSLIを定義JSONとして読み込むCookの不具合も確認・修正した。HLSL/HLSLIは定義のStageからコンパイルするため、定義JSONの読込対象からのみ除外する。新規ゲームのSampleMoverも旧`Time.deltaTime`から現行`Time.DeltaTime`へ合わせた。

製品ビルドスクリプト、Cook、NEMBuildTool、ランタイム配置の直接の進捗・エラー文は日本語化した。Windows PowerShell用スクリプトはUTF-8 BOM付きにし、MSBuild配下のランタイム配置は呼び出し元の文字コードを尊重する。機械判定用の`[NEM_GAME_BUILD_ERROR]`は変更しない。コンパイラー等の原文診断は保持する。

回帰検証は[SdkProductBuildRegression.ps1](../Project/Tools/Harness/SdkProductBuildRegression.ps1)。3構成を同梱したSDKで、ソースを含まない新規ゲームを生成し、製品出力・Cook・全ファイルハッシュ・開発用ファイルの除外・失敗時の既存製品保持を検査する。`-IncludeSourceBuild`で従来のツールビルド経路と製品の置換も確認する。検証用マニフェストはスクリプトで構成しており、エディターUIの収集・開始操作を自動化するものではない。結果は`Generated/SdkTests`配下へ保存する。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Project/Tools/Harness/SdkProductBuildRegression.ps1 -IncludeSourceBuild
```

最終検証は`Generated/SdkTests/46c9bcb9`。SDK・ソース経路とも38シェーダー、82ステージ、DXIL 4,841,932バイト、製品428ファイルのハッシュ検証に成功。C#ビルドは警告・エラー0件。全Engineアセットを使う検証では`sampleScene.renderFeatureProfile.json`の参照欠損警告が1件ある。参照先GUIDは`4e454d4153534554d4585b4b3c65da40`で、今回そのアセットは変更していない。GJ4の実プロジェクトでの再ビルド・GPU表示・SDKの公開と更新は別途確認が必要。

変更後のNEMEditor・SandboxはDebug/Develop/Release、NEMBuildToolはReleaseのビルドに成功。SDKは手動ビルド後に`PackageEngineSDK.ps1 -SkipBuild`で更新した。エンジン側のPremake再生成は不要で未実施。検証用ゲームのみ、新規プロジェクト生成のためテンプレートのPremakeを実行した。

### B. SDK更新がGitの途中失敗を取りこぼす

`Templates/GameProject/Tools/UpdateSdk.ps1`はfetch、reset、submodule updateを順に実行し、最後の`$LASTEXITCODE`だけで`$updateOk`を決める。fetch等が失敗して最後だけ成功した場合に、途中失敗が判定から落ちる経路がある。加えてreset --hardを含むため、このスクリプトの実行は読取確認ではない。今回はソース確認のみ。

### C. フォルダーとビルド上の所有者が一部一致しない

`premakeCommon.lua::NEM_AddCoreProjectFiles`はCore内のImGui/Particle GUIを除外し、Editor側でそれらとParticleのShape/Moduleをコンパイルしている。ファイルがCore配下だからCoreだけビルドすればよいとは限らない。Editor/Runtime両構成のコンパイル条件を確認する必要がある。

### 製品版の固定描画パスとCookの整合性修正

2026-09-05の製品描画不具合への対応。`LightingPass`がHLSLのファイル名だけを指定し、製品版で必要なCook用Shader GUIDを持っていなかった。通常・影付きのDeferred LightingをそれぞれShader Assetにし、共通Fullscreen VSとともにGUIDで読み込む。同じ直接指定を使っていたSkybox照明、深度表示、Skybox描画も対応した。既存のHLSL計算、描画順、開発時のソースコンパイル経路は変更していない。

- `BuiltinAssetIDs.h`の`Shaders::FixedRuntime`を、Build collectorとShaderCookの共通必須アセット一覧にする
- `GameBuildService.cpp`は上記Shader Assetを依存収集し、`windowSettings.exeConfig.json`をConfig除外の例外として同梱する
- `ShaderCook.cpp`は必須Shaderの欠落と有効ステージのないShaderを日本語エラーにして、製品生成を失敗させる
- `AssetDatabase.cpp`は製品実行時に限りShaderの`stages`/`sourceShader`とMaterialの`EditorPicking`を依存検査から除く。ディスク上のアセットや実際のMaterial Passは削除せず、Textureや実行用Pipelineの欠損検出は維持する

回帰検証を追加した。

- `SdkProductBuildRegression.ps1`: 固定描画の7ステージのGUID/entry/profile/DXILを検査。必須Shader欠落でも既存製品を保持する。`-GameSourceRoot`と`-StartupScene`でゲームのコピーを検証できる
- `ProductAssetDependencyRegression.ps1`: 開発中はソース・Pickingの欠損を検出し、製品では除外する一方、実行用Pipeline・Textureの欠損を引き続き検出する
- `GameBuildServiceProbe.cpp`と`.targets`: NEMTestsのビルド設定を再利用し、Menubarと同じ`GameBuildService::Start`による収集・ビルドをUIなしで検証する。出力と中間ファイルは専用ディレクトリに隔離する

Probeのビルドは、エンジンReleaseのビルド後に`Project/Tests/NEMTests.vcxproj`へ`/p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /p:ForceImportBeforeCppTargets=<絶対パス>/Project/Tools/Harness/GameBuildServiceProbe.targets`を指定する。実行引数は`<コピーしたゲームルート> <開始シーンGUID> <検証用出力先>`。SDK検証では`NEMENGINE_ROOT`をSDKへ設定し、Windows PowerShellから起動する。PowerShell 7のモジュール検索パスを直接継承すると、子のWindows PowerShellで`Get-FileHash`が見つからない検証環境上の問題が起こり得る。

確認済みの結果:

- GJ4のコピーから共通Build serviceで36 Shader / 62ステージをCookし、604製品ファイルのハッシュ検証に成功
- `Generated/SdkTests/2461ef9a/CollectedProducts/ProductProbe`の起動ログで、Deferred Lightingの通常・影付き読み込み成功、Pipeline作成失敗0件、Cook欠落0件を確認。設定ファイルも同梱
- `Generated/SdkTests/eba0e29a`でSDK単体の製品生成、7ステージ検証、日本語エラー、失敗時の既存製品保持が成功
- NEMTestsの`--render-features`、`--materials`、`--shader-graph`が成功。実行時の作業ディレクトリは`Project/Sandbox`
- NEMEditor/SandboxをDebug・Develop・Releaseでビルドし、NEMBuildTool/NEMTests/ProbeをReleaseでビルド。Premake再生成とBinding検証も成功
- Developの通常リンクは他プロセスが`NEMEditor.pdb`を占有して失敗したため、検証時のみ`Generated/BuildVerification/ProductBuildFixSymbols.targets`でPDBを別名出力して成功。ユーザーのEditor/Visual Studioは停止していない
- `Tools/PackageEngineSDK.ps1 -SkipBuild`で3構成のSDKを再出力し、各構成のEditor/Runtimeがビルド成果物と同一ハッシュであることを確認

GJ4本体の編集・SDK公開は行っていない。ゲーム側の既存PostProcess参照12件とGimmickIconの参照1件は元データでも欠損しており、本修正では補完していない。製品ではコンパイル済みC#に置き換わるScriptソースへの参照警告も別途残る。全アセットを手動で同梱した検証では未使用の旧MeshOutlineのPSO失敗が出るが、共通Build serviceの収集結果では出ない。ログ上の成立確認と全画面・全ゲーム操作の見た目一致は区別する。

### D. API名だけから更新時刻を推測しない

`WaitForEndOfFrame`のCoroutine再開は現在BehaviorSystemのLateUpdate末。後続SystemのLateUpdateと描画はその後にある。画像取得や描画後処理を追加する際は、この名称だけを描画完了保証として使わない。今回は仕様変更していない。

### E. 既存自動テストにも境界がある

`NEMTests --scene-lifecycle`はScene変更後のHeaderと通知を検査するNativeテストであり、実際のC# Assemblyの全Lifecycleを実行するテストではない。`--physics`にも壁接触ジャンプの2D/3D全経路を保証する根拠はない。対象のゲーム挙動を確認するときは追加の実行確認が必要。

## 機能追加時の変更・検証マップ

| 依頼内容 | 合わせて追う実装 | 必要になる回帰確認 |
|---|---|---|
| Component追加・項目変更 | Component/Storage hook → Manifest → Binding → SerializeECS → Drawer | 既存保存値、既定値、複製、Edit/Play、C#操作、`--ecs` |
| C# API追加 | ABI JSON/types → Bridge → ScriptCore → registration → SDK Managed | 双方ABI、stale handle、入力値、呼び出し時刻、SDK版 |
| Scene/Prefab | SceneSystem/InstanceManager、Remapper、Override、Command、Build collector | 保存往復、nested、参照、Undo/Redo、Single/Additive/Unload |
| Render機能 | Component/通知 → Extractor → Material/Pipeline → Backend/Pass → HLSL | 全Renderer/View、非対応GPU経路、Depth/Picking、Cook |
| Shader/Graph | Graph/IR/Artifact、各Pass、reflection、PSOキー、reload | 複数Pass、parameter ID、Sampler、`--shader-graph`、DXC |
| Editor UI | Registry/Drawer/Command/Context/dirty/layout | Undo/Redo、保存、Prefab stage、複数選択、Play停止 |
| SDK・Premake | source/template/Generated SDK、Debugger、Managed配置、BuildGame | 3構成、ソースなしゲーム、F5、C# breakpoint、製品生成 |

既存の検証入口は[Project/Tests/main.cpp](../Project/Tests/main.cpp)と[RefactoringRegression.md](../Project/Tools/Harness/RefactoringRegression.md)。NEMTestsには`--ecs`、`--prefab`、`--prefab-immediate`、`--prefab-nested`、`--scene-lifecycle`、`--physics`、`--canvas-ui`、`--paths`、`--texture-import`、`--shader-graph`、`--materials`、`--render-features`、`--render-feature-profile`がある。fixtureのファイル操作とRuntimePathsの作業ディレクトリ依存を読んでから実行する。

## 初回調査で実施した確認と実施していない確認

| 確認 | 結果 |
|---|---|
| 上記の主要入口・所有者・更新順・保存／配布経路 | 現行ソースを追跡 |
| ComponentBindingGenの既存DLLを`--verify`で実行 | 成功: components=61、bindings=25、abi=158。再生成・再ビルドなし |
| ABI versionのC++/C#定義 | 双方49 |
| Engine Assets、Binding JSON、Sandbox、TemplatesのJSON/meta/nemproject | 721ファイルのJSON構文解析成功 |
| Premake/Tools/TemplatesのPowerShell | 16ファイルの構文解析成功 |
| 生成済みSDKの配置・version情報 | 3構成の記載、2026-09-04 17:47 UTCの作成情報。実行互換性の保証ではない |
| Premake実行 | 未実施。調査文書のみで再生成不要 |
| C++/C#/HLSLビルド、NEMTests実行 | 未実施。今回は構造・実装調査で、コード変更なし |
| Editor操作、GPU表示、GJ4、SDK公開／更新、製品ビルド実行 | 未実施。動作確認済みとは扱わない |

初回調査で追加したのはこの調査文書のみ。後続のSDK製品ビルド修正と検証は上記A節に記録した。今後はこの地図を入口として、依頼された機能の現行差分と具体的な呼び出し経路を再確認してから実装する。
