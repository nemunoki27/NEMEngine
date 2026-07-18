#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// ECSシステム
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Rendering/UVTransformSystem.h>
#include <Engine/Core/World/Systems/Rendering/FlipbookAnimationSystem.h>
#include <Engine/Core/World/Systems/Rendering/FillFaceMeshRendererSystem.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/UI/UIInputSystem.h>
#include <Engine/Core/World/Systems/UI/UICanvasSystem.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>

// プレファブ編集の環境複製/メンバー判定で参照するコンポーネント
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>

// c++
#include <algorithm>
#include <unordered_set>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioSourceSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraControllerSystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>

//============================================================================
//	EngineApplication classMethods
//============================================================================
namespace {

	constexpr const char* kActiveSceneConfigPath = Engine::ConfigPaths::kActiveScene;
	constexpr const char* kFrameRateConfigPath = Engine::ConfigPaths::kFrameRate;
	// デフォルトマテリアル設定はチームで共有したいのでgit管理されるGameAssets配下へ置く
	constexpr const char* kDefaultMaterialConfigPath = "GameAssets/Materials/Config/defaultMaterials.materialSettings.json";

	Engine::EngineApplication* g_activeEngineApplication = nullptr;

	bool RequestEngineApplicationClose() {

		if (!g_activeEngineApplication) {
			return true;
		}
		return g_activeEngineApplication->RequestClose();
	}

	void NotifyEngineApplicationAssert() {

		if (!g_activeEngineApplication) {
			return;
		}
		g_activeEngineApplication->NotifyAssertBeforeAbort();
	}
}

void Engine::EngineApplication::InitSystems() {

	int32_t order = 0;
	// システムの追加、orderが小さいほど先に処理される
	scheduler_.AddSystem(std::make_unique<HierarchySystem>(), ++order);
	// UI入力はBehaviorより先に確定し、C#のUpdateから同フレームのクリックを参照できるようにする
	scheduler_.AddSystem(std::make_unique<UIInputSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<BehaviorSystem>(), ++order);
	// プロパティアニメはスクリプトの後で適用し、LateUpdateのTransform確定前に値を書く
	scheduler_.AddSystem(std::make_unique<AnimationPlayerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<PhysicsSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<AudioSourceSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraControllerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<TransformSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CollisionSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<FlipbookAnimationSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<UVTransformSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<FillFaceMeshRendererSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<ParticleSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<SkinnedAnimationSystem>(), ++order);
	// ジョイント追従はスケルトン更新の後でないとジョイントのワールド行列が確定しないため、最後に動かす
	scheduler_.AddSystem(std::make_unique<JointAttachmentSystem>(), ++order);
	// Canvas行列は全Transform更新後に確定する
	scheduler_.AddSystem(std::make_unique<UICanvasSystem>(), ++order);
}

void Engine::EngineApplication::InitFirstScene() {

	// アクティブなシーンの表示・保存用パスはGUIDから引き直す
	if (const AssetMeta* meta = assetDataBase_.Find(activeScene_)) {
		activeScenePath_ = meta->assetPath;
	}
	// シーンをロードしてエディタワールドにインスタンスを作成
	editScenes_.LoadSceneTree(assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_);
}

void Engine::EngineApplication::LoadActiveSceneConfig() {

	// 前回終了時に開いていたシーンがあれば、初期シーンとして使う
	std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kActiveSceneConfigPath);
	if (!JsonAdapter::Check(configPath.string(), false)) {

		// 旧版はEngine/Assets/Config(SDK内)に保存していたので、移行のため旧パスも読む
		const std::filesystem::path legacyPath = RuntimePaths::GetEngineAssetPath(kActiveSceneConfigPath);
		if (!JsonAdapter::Check(legacyPath.string(), false)) {
			return;
		}
		configPath = legacyPath;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath.string(), false);
	if (!data.is_object()) {
		return;
	}

	AssetID sceneAsset = ParseAssetReference(data, "activeScene", &assetDataBase_, AssetType::Scene);
	if (!sceneAsset) {
		return;
	}

	const std::filesystem::path fullPath = assetDataBase_.ResolveFullPath(sceneAsset);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "EngineApplication: active scene config points missing scene. guid={}", ToString(sceneAsset));
		return;
	}
	activeScene_ = sceneAsset;
	if (const AssetMeta* meta = assetDataBase_.Find(sceneAsset)) {
		activeScenePath_ = meta->assetPath;
	}
}

void Engine::EngineApplication::SaveActiveSceneConfig() const {

	// SDK更新で消えないよう、ゲームルート配下のConfigへ小さなJSONで保存する
	nlohmann::json data = nlohmann::json::object();
	data["activeScene"] = ToAssetReferenceJson(activeScene_);

	const std::filesystem::path configPath = RuntimePaths::GetGameConfigPath(kActiveSceneConfigPath);
	JsonAdapter::Save(configPath.string(), data);
}

void Engine::EngineApplication::Init(GraphicsCore& graphicsCore) {

	g_activeEngineApplication = this;
	WinApp::SetCloseRequestCallback(RequestEngineApplicationClose);
	Assert::SetPreAssertHandler(NotifyEngineApplicationAssert);

	// アセットデータベース初期化
	assetDataBase_.Init();
	assetDataBase_.RebuildMeta();
	LoadActiveSceneConfig();

	// フレームレート上限を設定ファイルから読み込む
	FrameRateSettings::GetInstance().Load(RuntimePaths::GetGameConfigPath(kFrameRateConfigPath).string());
	// 描画タイプごとのデフォルトマテリアル設定をGameAssets配下から読み込む
	DefaultMaterialSettings::GetInstance().Load((RuntimePaths::GetGameRoot() / kDefaultMaterialConfigPath).string());

	// 骨アニメーション管理の初期化
	skinnedAnimationManager_.Init();
	// Audio管理の初期化
	Audio::GetInstance()->Init();

	// 最初のシーンを作成
	InitFirstScene();
	// C#スクリプトランタイム初期化
	ManagedScriptRuntime::GetInstance().Init();
	// EditWorldをスクリプトから参照可能にし生ポインタの代わりに世代付きハンドルを使う
	ManagedWorldRegistry::GetInstance().Register(worldManager_.GetEditWorld());
	if constexpr (BuildConfig::kEditorEnabled) {

		// Editモードの非同期build/reloadサービスを初期化しsource baselineとlast-known-goodを整える
		scriptBuildService_.Initialize(&ManagedScriptRuntime::GetInstance());
	}
	// システムの初期化
	InitSystems();

	// 描画パイプライン初期化
	renderPipeline_ = std::make_unique<RenderPipelineRunner>();
	renderPipeline_->Init();

	// ライン描画初期化
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	LineRenderer::GetInstance()->Init(graphicsCore);
#endif

	// エディタの初期化
	if constexpr (BuildConfig::kEditorEnabled) {

		editorManager_.Init(graphicsCore);

		// アセットの外部編集を非同期監視し、texture/modelを自動でホットリロードする
		assetWatchService_.Start(&assetDataBase_, &graphicsCore.GetTextureUploadService(),
			{ RuntimePaths::GetGameRoot() / "GameAssets", RuntimePaths::GetEngineAssetsRoot() });
		// モデル変更時のリロードは描画バックエンドのメッシュ管理へ委譲する
		assetWatchService_.SetMeshReloadCallback([this](AssetID meshAssetID) {
			if (renderPipeline_) {
				renderPipeline_->ReloadMesh(meshAssetID);
			}
			});
		// Material/Shader/Pipeline変更時は依存PSOを含めて再ロードする
		assetWatchService_.SetRenderAssetReloadCallback([this](AssetID assetID) {
			if (renderPipeline_) {
				renderPipeline_->ReloadAsset(assetDataBase_, assetID);
			}
			});
	} else {

		// Releaseはエディタ操作を待たず、起動時のシーンからPlayWorldを開始する
		StartPlayWorld();
	}
}

const Engine::SceneHeader* Engine::EngineApplication::GetActiveSceneHeader() {

	// In-Context編集ではhostシーンを参照する
	const SceneInstance* instance = GetActiveScenes().GetActive();
	return instance ? &instance->header : nullptr;
}

void Engine::EngineApplication::EnterPrefabEdit(AssetID prefabAsset) {

	// プレファブ以外や無効IDは無視する、Play中は呼ばれない前提
	const AssetMeta* meta = assetDataBase_.Find(prefabAsset);
	if (!meta || meta->type != AssetType::Prefab) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: EnterPrefabEdit ignored. asset is not a prefab.");
		return;
	}

	// 隔離ワールドを新規に作り、そこへプレファブだけを展開する、デフォルトは隔離編集
	PrefabEditStage stage{};
	stage.asset = prefabAsset;
	stage.world = std::make_unique<ECSWorld>();
	stage.name = std::filesystem::path(meta->assetPath).stem().string();
	stage.inContext = false;
	// 編集前のベースを控えておき、退出時にインスタンスへ伝播する際のオーバーライド判定に使う
	stage.baseAtEnter = PrefabOverrideUtility::LoadPrefabBaseEntities(assetDataBase_, prefabAsset);
	// 編集セッションを束ねるインスタンスID、In-Context切替後もヒエラルキー絞り込みに使う
	stage.instanceID = UUID::New();

	// 遷移元(Editまたは親プレファブ)のワールドとシーンを、In-Context置き場と環境複製元として控える
	stage.hostWorld = GetActiveWorld();
	const SceneInstance* baseScene = GetActiveScenes().GetActive();
	stage.hostSceneInstanceID = baseScene ? baseScene->instanceID : UUID{};
	const SceneHeader baseHeader = baseScene ? baseScene->header : SceneHeader{};

	// 隔離ワールドに一時シーンを作り、プレファブを編集用に展開する、これが無いと描画でactiveSceneが無くビュー更新されない
	const UUID sceneInstanceID = stage.scenes.CreateScratchScene(baseHeader);
	PrefabInstantiateResult result{};
	if (!MaterializePrefabForEdit(*stage.world, prefabAsset, sceneInstanceID, stage.instanceID, result)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to enter prefab edit. instantiate failed. name={}", stage.name);
		return;
	}
	stage.root = result.root;

	// 遷移元の3Dカメラと平行光源を環境として隔離ワールドへ複製する、保存対象にもヒエラルキーにも出さない
	CopyPrefabEditEnvironment(*stage.world, stage.hostWorld, sceneInstanceID, stage.environmentEntities);

	// 隔離ワールドへ切り替わるので、別ワールドのエンティティを指す選択や履歴を片付ける
	editorManager_.ResetSceneEditingState();

	// 末尾を現在の編集対象としてスタックへ積む、ネスト編集はこの上にさらに積む
	prefabStages_.emplace_back(std::move(stage));
}

bool Engine::EngineApplication::MaterializePrefabForEdit(ECSWorld& world, AssetID prefabAsset,
	UUID sceneInstanceID, UUID instanceID, PrefabInstantiateResult& outResult) {

	// プレファブ自身のlocalFileIDをそのまま使う恒等remapを作る
	// これで編集→保存でlocalFileIDが変わらず、既存インスタンスのオーバーライド参照が壊れない
	const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(assetDataBase_, prefabAsset);
	std::vector<std::pair<UUID, UUID>> identityRemap;
	identityRemap.reserve(base.size());
	for (const auto& [localID, baseEntity] : base) {
		identityRemap.emplace_back(localID, localID);
	}

	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = sceneInstanceID;
	desc.forcedInstanceID = instanceID;
	desc.localFileIDRemap = &identityRemap;
	return prefabSystem.InstantiatePrefab(assetDataBase_, hierarchySystem, world, prefabAsset, outResult, desc) &&
		world.IsAlive(outResult.root);
}

void Engine::EngineApplication::CopyPrefabEditEnvironment(ECSWorld& targetWorld, ECSWorld* sourceWorld,
	UUID sceneInstanceID, std::vector<Entity>& outEnvironmentEntities) {

	if (!sourceWorld) {
		return;
	}

	// サブツリーをスナップショット経由でワールド間コピーし、一時シーンへ所属させて描画/ライティングへ反映する
	HierarchySystem hierarchySystem{};
	auto copyEnvironment = [&](const Entity& sourceEntity) {

		if (!sourceWorld->IsAlive(sourceEntity)) {
			return;
		}
		EditorEntityTreeSnapshot snapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(*sourceWorld, sourceEntity, snapshot);
		std::vector<Entity> restored = EditorEntitySnapshotUtility::RestoreSubtree(targetWorld, snapshot);

		for (const Entity& entity : restored) {

			if (targetWorld.HasComponent<SceneObjectComponent>(entity)) {
				targetWorld.GetComponent<SceneObjectComponent>(entity).sceneInstanceID = sceneInstanceID;
			}
			outEnvironmentEntities.emplace_back(entity);
		}
		hierarchySystem.RebuildRuntimeLinks(targetWorld, restored);
		};

	// 最初に見つかった3Dカメラと平行光源を複製する
	Entity sourceCamera = Entity::Null();
	sourceWorld->ForEach<PerspectiveCameraComponent>([&](const Entity& entity, PerspectiveCameraComponent&) {
		if (!sourceCamera.IsValid()) { sourceCamera = entity; }
		});
	Entity sourceLight = Entity::Null();
	sourceWorld->ForEach<DirectionalLightComponent>([&](const Entity& entity, DirectionalLightComponent&) {
		if (!sourceLight.IsValid()) { sourceLight = entity; }
		});
	copyEnvironment(sourceCamera);
	copyEnvironment(sourceLight);
}

void Engine::EngineApplication::ExitPrefabEdit() {

	if (prefabStages_.empty()) {
		return;
	}
	// 退出処理と伝播に必要な情報を退出前に控える
	PrefabEditStage& top = prefabStages_.back();
	const AssetID editedAsset = top.asset;
	const std::unordered_map<UUID, PrefabBaseEntity> oldBase = top.baseAtEnter;
	const bool wasInContext = top.inContext;
	ECSWorld* hostWorld = top.hostWorld;
	const UUID editInstanceID = top.instanceID;

	// 退出時は現在の編集内容を元の.prefabへ自動保存する
	SaveCurrentPrefab();

	if (wasInContext) {

		// In-Context編集はセッション専用instanceIDの一時実体だけ破棄する、別IDの元シーン実インスタンスには触れない
		if (hostWorld) {
			const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*hostWorld, editInstanceID);
			for (const Entity& member : members) {
				if (hostWorld->IsAlive(member)) {
					EditorEntitySnapshotUtility::DestroySubtree(*hostWorld, member);
				}
			}
		}
		prefabStages_.pop_back();
	} else {

		// 隔離編集はワールドごと破棄する、Schedulerが指したままだと次TickのDetachWorldがdangling参照になる
		scheduler_.DetachCurrentWorld(systemContext_);
		prefabStages_.pop_back();
	}

	// 戻り先の元シーンのインスタンスへ編集を反映する、再生成失敗時はバックアップ復元で実体を失わない
	if (ECSWorld* targetWorld = GetActiveWorld()) {
		PropagatePrefabToInstances(*targetWorld, editedAsset, oldBase);
	}

	// 破棄したワールドのエンティティを指す選択や履歴を片付ける
	editorManager_.ResetSceneEditingState();
}

void Engine::EngineApplication::ExitAllPrefabEdit() {

	// 各階層を保存・伝播しながら全て抜け、一回の操作で元のシーン編集へ戻す
	while (!prefabStages_.empty()) {
		ExitPrefabEdit();
	}
}

void Engine::EngineApplication::TogglePrefabInContextMode() {

	if (prefabStages_.empty()) {
		return;
	}
	PrefabEditStage& top = prefabStages_.back();

	// 切り替え前に現在の編集内容を.prefabへ保存して、置き場が変わっても編集が失われないようにする
	SaveCurrentPrefab();

	const AssetID asset = top.asset;
	const UUID instanceID = top.instanceID;

	if (!top.inContext) {

		// 隔離 -> In-Context、隔離ワールドを捨ててhostWorldへ展開する
		scheduler_.DetachCurrentWorld(systemContext_);
		top.world.reset();
		top.scenes = SceneInstanceManager{};
		top.environmentEntities.clear();
		top.inContext = true;

		PrefabInstantiateResult result{};
		if (top.hostWorld && MaterializePrefabForEdit(*top.hostWorld, asset, top.hostSceneInstanceID, instanceID, result)) {
			top.root = result.root;
		}
	} else {

		// In-Context -> 隔離、hostWorldの一時実体をinstanceIDで全て捨ててから隔離ワールドへ展開する
		// ルート外や複数ルートの実体も漏らさず消し、元シーンへ残さない
		if (top.hostWorld) {
			const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*top.hostWorld, instanceID);
			for (const Entity& member : members) {
				if (top.hostWorld->IsAlive(member)) {
					EditorEntitySnapshotUtility::DestroySubtree(*top.hostWorld, member);
				}
			}
		}
		top.inContext = false;
		top.world = std::make_unique<ECSWorld>();

		const SceneInstance* hostScene = ResolveHostScenes(top).Find(top.hostSceneInstanceID);
		const SceneHeader baseHeader = hostScene ? hostScene->header : SceneHeader{};
		const UUID sceneInstanceID = top.scenes.CreateScratchScene(baseHeader);

		PrefabInstantiateResult result{};
		if (MaterializePrefabForEdit(*top.world, asset, sceneInstanceID, instanceID, result)) {
			top.root = result.root;
		}
		CopyPrefabEditEnvironment(*top.world, top.hostWorld, sceneInstanceID, top.environmentEntities);
	}

	editorManager_.ResetSceneEditingState();
}

void Engine::EngineApplication::SaveCurrentPrefab() {

	if (prefabStages_.empty()) {
		return;
	}
	// 保存前に新規作成エンティティをプレファブのサブツリーへ取り込み、保存漏れを防ぐ
	SyncPrefabEditedEntities();

	PrefabEditStage& stage = prefabStages_.back();
	const AssetMeta* meta = assetDataBase_.Find(stage.asset);
	// In-Context編集ではhostWorld、隔離編集では隔離ワールドの実体を書き戻す
	ECSWorld* editWorld = stage.inContext ? stage.hostWorld : stage.world.get();
	// rootが削除されていても保存できるようにする、root健在チェックは保存ルート確定後に行う
	if (!meta || !editWorld) {
		return;
	}

	auto entityKey = [](const Entity& e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; };
	auto isHierarchyRoot = [&](const Entity& e) {
		return !editWorld->HasComponent<HierarchyComponent>(e) ||
			!editWorld->IsAlive(editWorld->GetComponent<HierarchyComponent>(e).parent);
		};

	// 保存ルートを決める、隔離編集は環境以外の全ルート、In-Contextは編集インスタンスのメンバールートのみ
	std::vector<Entity> saveRoots;
	if (stage.inContext) {

		const std::vector<Entity> members = PrefabOverrideUtility::CollectInstanceEntities(*editWorld, stage.instanceID);
		std::unordered_set<uint64_t> memberKeys;
		for (const Entity& member : members) {
			memberKeys.insert(entityKey(member));
		}
		for (const Entity& member : members) {

			Entity parent = editWorld->HasComponent<HierarchyComponent>(member) ?
				editWorld->GetComponent<HierarchyComponent>(member).parent : Entity::Null();
			if (!(editWorld->IsAlive(parent) && memberKeys.count(entityKey(parent)))) {
				saveRoots.emplace_back(member);
			}
		}
	} else {

		// 環境エンティティ(複製したカメラ/平行光源)を除いたルートを保存対象にする、新規作成した複数ルートも含む
		editWorld->ForEachAliveEntity([&](Entity entity) {

			if (!isHierarchyRoot(entity)) {
				return;
			}
			if (std::find(stage.environmentEntities.begin(), stage.environmentEntities.end(), entity) !=
				stage.environmentEntities.end()) {
				return;
			}
			saveRoots.emplace_back(entity);
			});
	}

	// 元のrootが削除されていたら、現在の保存ルートの先頭を新しいrootに採用する
	// これで全削除→追加した場合でも保存され、再オープン時に削除前のデータへ戻らない
	if (!editWorld->IsAlive(stage.root)) {
		stage.root = saveRoots.empty() ? Entity::Null() : saveRoots.front();
	}
	if (!editWorld->IsAlive(stage.root)) {
		// プレファブが完全に空、ヘッダのrootを決められないので保存しない
		return;
	}

	// 各保存ルートのサブツリー(追加した子も含む)を重複なく集める
	std::vector<Entity> saveEntities;
	std::unordered_set<uint64_t> seen;
	for (const Entity& root : saveRoots) {
		for (const Entity& entity : EditorEntitySnapshotUtility::CollectSubtreeEntities(*editWorld, root)) {
			if (seen.insert(entityKey(entity)).second) {
				saveEntities.emplace_back(entity);
			}
		}
	}

	PrefabSystem prefabSystem{};
	prefabSystem.SavePrefabFromEntities(assetDataBase_, *editWorld, stage.root, saveEntities, meta->assetPath);
}

void Engine::EngineApplication::SyncPrefabEditedEntities() {

	if (prefabStages_.empty()) {
		return;
	}
	PrefabEditStage& stage = prefabStages_.back();
	// In-Context編集ではhostWorldにシーンの実体も混在するため、誤って取り込まないよう自動取り込みは行わない
	// In-Contextでは新規実体をプレファブrootの子に手動で入れればSavePrefabのサブツリー収集で保存される
	if (stage.inContext || !stage.world) {
		return;
	}
	ECSWorld& world = *stage.world;

	// 編集セッションのインスタンスIDで束ねる、rootを全削除した後でも新規実体にPrefabLinkを付けて水色表示にする
	const UUID rootInstanceID = stage.instanceID;
	const bool hasRoot = world.IsAlive(stage.root);

	// ルートと環境エンティティ以外を対象にする、反復中の構造変更を避けて先に集める
	std::vector<Entity> targets;
	world.ForEachAliveEntity([&](Entity entity) {

		if (entity == stage.root) {
			return;
		}
		if (std::find(stage.environmentEntities.begin(), stage.environmentEntities.end(), entity) !=
			stage.environmentEntities.end()) {
			return;
		}
		targets.emplace_back(entity);
		});
	if (targets.empty()) {
		return;
	}

	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	for (const Entity& entity : targets) {

		if (!world.IsAlive(entity)) {
			continue;
		}

		// Unity準拠で1プレファブ1ルートを強制し、トップレベルになった実体はプレファブルート配下へ入れる
		const bool isRoot = !world.HasComponent<HierarchyComponent>(entity) ||
			!world.IsAlive(world.GetComponent<HierarchyComponent>(entity).parent);
		if (hasRoot && isRoot && !world.HasComponent<JointAttachmentComponent>(entity)) {
			hierarchySystem.SetParent(world, entity, stage.root);
		}

		// 新規作成と追加Prefabの実体を編集中プレファブのメンバーへ揃える
		const bool belongsToStage = world.HasComponent<PrefabLinkComponent>(entity) &&
			world.GetComponent<PrefabLinkComponent>(entity).prefabAsset == stage.asset &&
			world.GetComponent<PrefabLinkComponent>(entity).prefabInstanceID == rootInstanceID;
		if (!belongsToStage) {

			UUID prefabLocalFileID{};
			if (world.HasComponent<SceneObjectComponent>(entity)) {
				prefabLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
			}
			prefabSystem.SetPrefabLink(world, entity, stage.asset, prefabLocalFileID, rootInstanceID, false);
		}
	}
}

void Engine::EngineApplication::PropagatePrefabToInstances(ECSWorld& world, AssetID prefabAsset,
	const std::unordered_map<UUID, PrefabBaseEntity>& oldBase) {

	// シーンロード時の展開と同じ経路を再利用する
	HierarchySystem hierarchySystem{};
	PrefabOverrideUtility::PropagateToInstances(world, assetDataBase_, hierarchySystem, prefabAsset, oldBase);
}

Engine::RenderFrameRequest Engine::EngineApplication::BuildRenderFrameRequest(
	GraphicsCore& graphicsCore, ECSWorld* world, const SceneHeader* header) {

	// エディタの状態を描画要求へ変換する
	RenderFrameRequest request{};
	request.header = header;
	request.world = world;
	// 描画側がECSシステムと同じフレーム情報を参照できるように渡す
	request.systemContext = &systemContext_;
	request.assetDatabase = &assetDataBase_;

	// Play->プレファブ編集->Editの順でシーンインスタンスを切り替える
	SceneInstanceManager* activeScenes = &GetActiveScenes();
	const SceneInstance* activeInstance = activeScenes->GetActive();
	request.sceneInstances = activeScenes;
	request.activeSceneInstanceID = activeInstance ? activeInstance->instanceID : UUID{};

	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	// GameView/SceneViewは同じ固定解像度を基準に描画サーフェイスを作る
	uint32_t fixedRenderWidth = windowSetting.gameSize.x;
	uint32_t fixedRenderHeight = windowSetting.gameSize.y;

	// エディタの状態に応じて描画ビューの要求を構築する
	bool showGameView = true;
	bool showSceneView = false;
	SceneViewCameraSelection sceneViewCameraSelection{};
	ManualRenderCameraState manualSceneCamera{};

	// エディタが有効な場合はエディタのレイアウト状態に応じてビューの要求を構築する
	if constexpr (BuildConfig::kEditorEnabled) {

		const EditorLayoutState& layout = editorManager_.GetLayoutState();
		if (layout.hidePanels) {

			// HidePanels中はReleaseと同じくGameViewだけを描画対象にする
			showGameView = true;
			showSceneView = false;
		} else {

			showGameView = layout.showGameView;
			showSceneView = layout.showSceneView;
			sceneViewCameraSelection = editorManager_.GetSceneViewCameraSelection();
			manualSceneCamera = editorManager_.GetSceneViewCameraState();
			request.drawSceneViewDefaultGrid = editorManager_.ShouldDrawSceneViewDefaultGrid();
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
			request.requireRaytracingSceneForEditorPicking =
				graphicsCore.GetDXObject().GetFeatureController().GetSupport().SupportsRayTracingPath();
#endif
		}
	}
	// ゲームビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
		viewRequest.kind = RenderViewKind::Game;
		viewRequest.enabled = showGameView;
		viewRequest.width = showGameView ? fixedRenderWidth : 0;
		viewRequest.height = showGameView ? fixedRenderHeight : 0;
		viewRequest.sourceKind = RenderViewSourceKind::WorldCamera;
		viewRequest.preferredOrthographicCameraUUID = UUID{};
		viewRequest.preferredPerspectiveCameraUUID = UUID{};
	}
	// シーンビューの要求を構築
	{
		RenderViewRequest& viewRequest = request.views[static_cast<uint32_t>(RenderViewKind::Scene)];
		viewRequest.kind = RenderViewKind::Scene;
		viewRequest.enabled = showSceneView;
		viewRequest.width = showSceneView ? fixedRenderWidth : 0;
		viewRequest.height = showSceneView ? fixedRenderHeight : 0;
		viewRequest.manualCamera = manualSceneCamera;

		// Entity Cameraが指定されている場合だけWorld側のカメラを使う
		if (sceneViewCameraSelection.mode == SceneViewCameraMode::SelectedEntityCamera &&
			sceneViewCameraSelection.HasAnyAssignedCamera()) {

			viewRequest.sourceKind = RenderViewSourceKind::WorldCamera;
			viewRequest.preferredOrthographicCameraUUID = sceneViewCameraSelection.orthographicCameraUUID;
			viewRequest.preferredPerspectiveCameraUUID = sceneViewCameraSelection.perspectiveCameraUUID;
		} else {

			// 通常はエディタ用の手動カメラを使う
			viewRequest.sourceKind = RenderViewSourceKind::ManualCamera;
			viewRequest.preferredOrthographicCameraUUID = UUID{};
			viewRequest.preferredPerspectiveCameraUUID = UUID{};
		}
	}
	return request;
}

void Engine::EngineApplication::Tick(GraphicsCore& graphicsCore, float deltaTime) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// フレームごとのデバッグラインをリセットする
	LineRenderer::GetInstance()->BeginFrame();
	// 選択アウトラインのtemporary requestもフレーム単位でリセットする
	EditorSelectionOutlineRequestService::GetInstance().BeginFrame();
#endif

	// アセットの外部編集を非同期検知し、変更があればtexture/modelをホットリロードする
	assetWatchService_.Update();

	// システムコンテキストの更新
	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.deltaTime = deltaTime;
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;

	if constexpr (BuildConfig::kEditorEnabled) {

		// 非同期build/reload状態機械を進める、Play中はreloadを適用せず変更検知のdirtyのみ行う
		scriptBuildService_.Tick(worldManager_.IsPlaying());
	}

	// プレイモードの切り替え
	HandlePlayToggle();
	// Play中の一時停止、再開、1フレーム送りを処理する
	HandlePlayPauseRequests();
	// エディタから要求されたシーン操作
	HandleEditorSceneRequests();
	// Play/Stopやシーン操作後のActive Worldを、このフレームの各Contextへ反映する
	RefreshActiveWorldContext();
	{
		// Play開始直後の最初の1フレームは進めず、貫通の原因になる大きなdeltaを捨てる
		const bool skipFirstAdvance = playWorldJustStarted_;
		playWorldJustStarted_ = false;

		bool advancePlayTime = !skipFirstAdvance && ShouldAdvanceActiveWorld() && systemContext_.mode == WorldMode::Play;
		float rawDelta = (!skipFirstAdvance && ShouldAdvanceActiveWorld()) ? deltaTime : 0.0f;
		systemContext_.deltaTime = ManagedScriptRuntime::AdvanceTime(rawDelta, systemContext_.fixedDeltaTime, advancePlayTime);
		systemContext_.unscaledDeltaTime = rawDelta;
	}

	ECSWorld* world = systemContext_.world;
	const SceneHeader* header = systemContext_.activeSceneHeader;

	if constexpr (BuildConfig::kEditorEnabled) {

		// パネルをすべて非表示にする
		bool hidePanels = editorManager_.GetLayoutState().hidePanels;
		if (hidePanels) {

			const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
			Input::GetInstance()->SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f),
				windowSetting.engineSizeFloat, windowSetting.gameSizeFloat);
		}

		// エディタのフレーム開始処理
		editorManager_.BeginFrame(graphicsCore, editorContext_);
		HandleCloseRequestResult();
	}

	// 即時ライン描画は1フレームで消えるので、スクリプトが発行する前にクリアする
	LineImmediateBuffer::GetInstance().BeginFrame();

	// プレファブ編集中はこのフレームのUIで作られたエンティティをプレファブの一部へ取り込む
	// ECS更新の前に行い、親子付け後のワールド行列が同フレームで正しく計算されるようにする
	if (IsPrefabEditing()) {
		SyncPrefabEditedEntities();
	}

	// ECSシステムの更新
	if (ShouldAdvanceActiveWorld()) {

		FrameProfiler::ScopedSample ecsSample(FrameProfiler::Category::Ecs);
		// Play中にscript例外が出たらUnity風にEditへ戻すため、tick前後で例外storeのversionを比べる
		const bool playingThisTick = worldManager_.IsPlaying();
		const uint64_t scriptExceptionVersion = playingThisTick ?
			ManagedScriptExceptionStore::GetInstance().Version() : 0;
		scheduler_.Tick(GetActiveWorld(), systemContext_);
		if (playingThisTick &&
			ManagedScriptExceptionStore::GetInstance().Version() != scriptExceptionVersion) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"EngineApplication: script exception during Play. Returning to Edit mode.");
			StopPlayWorld();
			world = systemContext_.world;
			header = systemContext_.activeSceneHeader;
		}
	}
	if (HandleApplicationQuitRequest()) {

		world = systemContext_.world;
		header = systemContext_.activeSceneHeader;
	}
	if (playFrameStepRequested_) {

		playFrameStepRequested_ = false;
		systemContext_.deltaTime = 0.0f;
	}

	// C++ツールの更新、ECSシステム更新の後に行うことで衝突判定可視化等が更新後のワールド行列を参照する
	if constexpr (BuildConfig::kEditorEnabled) {
		if (!editorManager_.GetLayoutState().hidePanels) {

			ToolContext toolContext{};
			toolContext.world = world;
			toolContext.assetDatabase = &assetDataBase_;
			toolContext.systemContext = &systemContext_;
			toolContext.sceneInstances = editorContext_.sceneInstances;
			toolContext.activeSceneHeader = header;
			toolContext.activeSceneAsset = editorContext_.activeSceneAsset;
			toolContext.activeSceneInstanceID = editorContext_.activeSceneInstanceID;
			toolContext.activeScenePath = activeScenePath_;
			toolContext.isPlaying = worldManager_.IsPlaying();
			toolContext.canEditScene = !worldManager_.IsPlaying() && world;
			toolContext.deltaTime = deltaTime;
			ToolRegistry::GetInstance().Tick(toolContext);
		}
	}
}

bool Engine::EngineApplication::ConsumeFrameDeltaResetRequest() {

	const bool requested = requestFrameDeltaReset_;
	requestFrameDeltaReset_ = false;
	return requested;
}

void Engine::EngineApplication::Render(GraphicsCore& graphicsCore) {

	// テクスチャアップロードなど、描画前に確定したいGPUサービスを更新する
	graphicsCore.TickFrameServices();

	// バックバッファ描画クリア
	graphicsCore.Render();

	if constexpr (BuildConfig::kEditorEnabled) {

		if (!editorManager_.GetLayoutState().hidePanels) {

			// SceneViewに重ねる選択エンティティのデバッグラインを、SceneView描画前に積む
			editorManager_.DrawSceneDebugObjects(editorContext_);
		}
	}

	// ワールドを描画
	renderPipeline_->Render(graphicsCore, BuildRenderFrameRequest(graphicsCore, GetActiveWorld(), GetActiveSceneHeader()));

	if constexpr (BuildConfig::kEditorEnabled) {

		const bool hidePanels = editorManager_.GetLayoutState().hidePanels;
		if (hidePanels) {

			// エディターUIを経由せず、Release時と同じGameViewの全画面表示にする
			renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
		} else {

			// シーンビューのメッシュピック処理
			editorManager_.ExecuteSceneMeshPicking(graphicsCore, editorContext_, *renderPipeline_);
		}

		// エディタのフレーム終了処理
		editorManager_.EndFrame(graphicsCore, editorContext_, &renderPipeline_->GetViewportRenderService(),
			&renderPipeline_->GetResolvedView(RenderViewKind::Scene), renderPipeline_.get());
	} else {

		// エディタがない場合はゲームビューをバックバッファに描画する
		renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
	}
}

void Engine::EngineApplication::HandlePlayToggle() {

	// Releaseは常にゲーム実行中のためF5でPlayWorldを停止させない
	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	}

	// Play開始をbuild/reload完了まで保留している間は、新規トグルを捨てて完了を待つ
	if (pendingPlayStart_) {

		// 保留解決後に古いトグル要求で誤Stopしないよう、要求は読み捨てる
		(void)Input::GetInstance()->TriggerKey(DIK_F5);
		if constexpr (BuildConfig::kEditorEnabled) {
			(void)editorManager_.ConsumePlayToggleRequest();
		}
		ProcessPendingPlayStart();
		return;
	}

	// プレイ/ストップの切り替え要求があるか
	bool requestedByKeyboard = Input::GetInstance()->TriggerKey(DIK_F5);
	bool requestedByEditor = false;

	// エディタがある場合はエディタからの要求も確認する
	if constexpr (BuildConfig::kEditorEnabled) {
		requestedByEditor = editorManager_.ConsumePlayToggleRequest();
	}
	// どちらからの要求もなければなにもしない
	if (!requestedByKeyboard && !requestedByEditor) {
		return;
	}
	// Playトグル処理にはビルド/ロード待ちが含まれ得るため、次フレームのdeltaTime基準を更新する
	requestFrameDeltaReset_ = true;

	if (!worldManager_.IsPlaying()) {

		// Play開始要求で最新のbuild/reloadを要求して保留し、Editor main threadをblockしない
		// pending dirty / build / reloadがあれば完了までPlay遷移を待つ
		scriptBuildService_.RequestPlayBuild();
		pendingPlayStart_ = true;
		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Play requested. preparing GameScripts (build/reload)...");
		// 同フレームで既にビルド対象なし等で最新なら即Play開始を試みる
		ProcessPendingPlayStart();
	} else {

		StopPlayWorld();
	}
}

void Engine::EngineApplication::StopPlayWorld() {

	// 実行中WorldからSchedulerを切り離して、PlayWorldを破棄する
	scheduler_.DetachCurrentWorld(systemContext_);
	// 破棄前にレジストリから解除し、古いハンドルが新しいPlayWorldを指さないようにする
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(*playWorld));
	}
	worldManager_.DestroyPlayWorld();
	playScenes_ = SceneInstanceManager{};
	playPaused_ = false;
	playFrameStepRequested_ = false;
	// OnDisableやOnDestroyから出た終了要求を次のPlayへ持ち越さない
	(void)ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest();
	RefreshActiveWorldContext();
}

bool Engine::EngineApplication::HandleApplicationQuitRequest() {

	if (!ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest()) {
		return false;
	}

	if constexpr (BuildConfig::kEditorEnabled) {

		if (!worldManager_.IsPlaying()) {
			return false;
		}
		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Application.Quit requested. Returning to Edit mode.");
		requestFrameDeltaReset_ = true;
		StopPlayWorld();
		return true;
	} else {

		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Application.Quit requested. Closing application.");
		WinApp::RequestCloseWindow();
		return false;
	}
}

void Engine::EngineApplication::RefreshActiveWorldContext() {

	ECSWorld* world = GetActiveWorld();
	const SceneHeader* header = GetActiveSceneHeader();
	SceneInstanceManager& activeScenes = GetActiveScenes();
	const SceneInstance* activeSceneInstance = activeScenes.GetActive();

	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	systemContext_.world = world;

	if (world) {

		WorldCommandServices services{};
		services.assetDatabase = &assetDataBase_;
		services.sceneInstances = &activeScenes;
		services.sceneSystem = &sceneSystem_;
		world->SetCommandServices(services);
	}

	systemContext_.activeSceneHeader = header;
	CollisionSettings::GetInstance().BindGlobal(systemContext_.assetDatabase);

	if constexpr (BuildConfig::kEditorEnabled) {

		editorContext_.isPlaying = worldManager_.IsPlaying();
		editorContext_.isPlayPaused = playPaused_;
		editorContext_.activeScenePath = activeScenePath_;
		editorContext_.activeSceneDirty = editorManager_.IsActiveSceneDirty();
		editorContext_.activeSceneHeader = header;
		editorContext_.activeSceneAsset = activeSceneInstance ? activeSceneInstance->sceneAsset : activeScene_;
		editorContext_.activeSceneInstanceID = activeSceneInstance ? activeSceneInstance->instanceID : UUID{};
		editorContext_.sceneInstances = &activeScenes;
		editorContext_.activeWorld = world;
		editorContext_.editWorld = &worldManager_.GetEditWorld();
		editorContext_.assetDatabase = &assetDataBase_;
		editorContext_.scriptBuildService = &scriptBuildService_;

		editorContext_.isPrefabEditing = IsPrefabEditing();
		editorContext_.prefabEditDepth = static_cast<int>(prefabStages_.size());
		editorContext_.prefabEditName = prefabStages_.empty() ? std::string{} : prefabStages_.back().name;
		editorContext_.prefabEditAsset = prefabStages_.empty() ? AssetID{} : prefabStages_.back().asset;
		editorContext_.prefabEditInstanceID = prefabStages_.empty() ? UUID{} : prefabStages_.back().instanceID;
		editorContext_.isPrefabInContext = !prefabStages_.empty() && prefabStages_.back().inContext;
		editorContext_.prefabInContextInstanceID =
			editorContext_.isPrefabInContext ? prefabStages_.back().instanceID : UUID{};
		editorContext_.prefabEnvironmentEntities =
			(!prefabStages_.empty() && !prefabStages_.back().inContext) ? &prefabStages_.back().environmentEntities : nullptr;
	}
}

void Engine::EngineApplication::ProcessPendingPlayStart() {

	// build/reloadの完了を待ち、Pendingの間はEditor tickを継続して保留する
	const ManagedScriptBuildService::PlayBuildResult result = scriptBuildService_.PollPlayBuild();
	if (result == ManagedScriptBuildService::PlayBuildResult::Pending) {
		return;
	}

	pendingPlayStart_ = false;

	// build/reload失敗時はEditモードを維持し、エラーを表示する
	if (result == ManagedScriptBuildService::PlayBuildResult::Failed) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: Play canceled. GameScripts build/reload failed. Staying in Edit mode.");
		return;
	}

	// 成功→ Playを開始する
	StartPlayWorld();
}

void Engine::EngineApplication::StartPlayWorld() {

	auto& scriptRuntime = ManagedScriptRuntime::GetInstance();

	// managed debuggerのattach待ちはユーザーの明示オプションで、この場合だけ
	// 現在ロード済みアセンブリをwait付きで読み直す、debugger attachを待つため意図的に同期
	bool waitForManagedDebuggerOnPlay = false;
	if constexpr (BuildConfig::kEditorEnabled) {
		waitForManagedDebuggerOnPlay = editorManager_.GetLayoutState().waitForManagedDebuggerOnPlay;
	}
	if (waitForManagedDebuggerOnPlay && !scriptRuntime.ActiveAssemblyPath().empty()) {
		scriptRuntime.LoadGameAssemblyFromPath(scriptRuntime.ActiveAssemblyPath(), true);
	}

	// EditWorldを直接Playへ使わず、JSONスナップショットからPlayWorldを作る
	nlohmann::json snapshot = editScenes_.SerializeSnapshot(sceneSystem_, worldManager_.GetEditWorld());

	// Play開始用のWorldを作成し、スナップショットからシーン状態を復元する
	worldManager_.CreatePlayWorld();
	if (!worldManager_.GetPlayWorld() ||
		!playScenes_.LoadSnapshot(assetDataBase_, sceneSystem_, *worldManager_.GetPlayWorld(), snapshot)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to enter Play mode. Scene snapshot load failed.");

		playScenes_ = SceneInstanceManager{};
		worldManager_.DestroyPlayWorld();
		playPaused_ = false;
		playFrameStepRequested_ = false;
		return;
	}
	// PlayWorldをスクリプトから参照可能にし最初のTickのPrepareより前に登録する
	ManagedWorldRegistry::GetInstance().Register(*worldManager_.GetPlayWorld());
	// gameplay time serviceを初期化しPlayWorldのTimeScaleComponentがあれば初期scaleとして読む
	ManagedScriptRuntime::BeginPlayTime(worldManager_.GetPlayWorld());
	playPaused_ = false;
	playFrameStepRequested_ = false;
	// 実際にPlayWorldが立ち上がったこのフレームでdeltaをリセットし、最初の1フレームは進めない
	requestFrameDeltaReset_ = true;
	playWorldJustStarted_ = true;
}

void Engine::EngineApplication::HandlePlayPauseRequests() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	} else {

		const bool resumeRequested = editorManager_.ConsumePlayResumeRequest();
		const bool pauseRequested = editorManager_.ConsumePlayPauseRequest();
		const bool frameStepRequested = editorManager_.ConsumePlayFrameStepRequest();

		if (!worldManager_.IsPlaying()) {
			playPaused_ = false;
			playFrameStepRequested_ = false;
			return;
		}
		if (resumeRequested) {
			playPaused_ = false;
		}
		if (pauseRequested) {
			playPaused_ = true;
		}
		if (frameStepRequested && playPaused_) {
			playFrameStepRequested_ = true;
		}
	}
}

bool Engine::EngineApplication::ShouldAdvanceActiveWorld() const {

	return !worldManager_.IsPlaying() || !playPaused_ || playFrameStepRequested_;
}

void Engine::EngineApplication::HandleEditorSceneRequests() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	} else {

		// EditorManagerに溜まっているシーン操作要求を1件取り出す
		EditorSceneRequest request = editorManager_.ConsumeSceneRequest();
		if (request.type == EditorSceneRequestType::None) {
			return;
		}
		// Play中はEditWorldを書き換えない
		if (worldManager_.IsPlaying()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: scene operation is ignored while playing.");
			return;
		}

		switch (request.type) {
		case EditorSceneRequestType::NewScene:
			// 空のGameシーンを作成して開く
			CreateNewEditScene();
			break;
		case EditorSceneRequestType::OpenScene:
			// Project上の既存シーンを開く
			OpenEditScene(request.sceneAsset);
			break;
		case EditorSceneRequestType::SaveScene:
			// 現在のEditシーンを保存する
			SaveActiveEditScene();
			break;
		case EditorSceneRequestType::SaveAndNewScene:
			// 保存に成功した場合だけ新規シーン作成へ進む
			if (SaveActiveEditScene()) {
				CreateNewEditScene();
			}
			break;
		case EditorSceneRequestType::SaveAndOpenScene:
			// 保存に成功した場合だけ別シーンを開く
			if (SaveActiveEditScene()) {
				OpenEditScene(request.sceneAsset);
			}
			break;
		case EditorSceneRequestType::EnterPrefabEdit:
			// 既にPrefab編集中なら、現在の編集内容を保存してから次のPrefabを開く
			if (IsPrefabEditing()) {
				SaveCurrentPrefab();
			}
			// プレファブを隔離ワールドへ展開して編集モードへ入る、ネストも可
			EnterPrefabEdit(request.sceneAsset);
			break;
		case EditorSceneRequestType::ExitPrefabEdit:
			// 現在のプレファブ編集を保存して1階層戻る
			ExitPrefabEdit();
			break;
		case EditorSceneRequestType::ExitPrefabEditAll:
			// プレファブ編集を一括で抜けて元のシーン編集へ戻る
			ExitAllPrefabEdit();
			break;
		case EditorSceneRequestType::TogglePrefabInContext:
			// In-Context編集のオンオフを切り替える
			TogglePrefabInContextMode();
			break;
		case EditorSceneRequestType::SavePrefab:
			// 現在のプレファブ編集を保存する、退出はしない
			SaveCurrentPrefab();
			break;
		case EditorSceneRequestType::None:
		default:
			break;
		}
	}
}

bool Engine::EngineApplication::CreateNewEditScene() {

	// GameAssets/Scenes配下に重複しないシーンファイルを作成する
	ProjectAssetFileResult result = ProjectAssetFileUtility::Create(
		ProjectAssetSource::Game,
		"GameAssets/Scenes",
		ProjectAssetFileKind::Scene,
		"NewScene");
	if (!result.success) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to create new scene. message={}", result.message);
		return false;
	}

	// 作成したシーンをAssetDatabaseへ登録し、開く処理へ渡す
	const AssetID sceneAsset = assetDataBase_.ImportOrGet(result.assetPath, AssetType::Scene);
	assetDataBase_.RebuildMeta();
	return OpenEditScene(sceneAsset);
}

bool Engine::EngineApplication::OpenEditScene(AssetID sceneAsset) {

	// AssetDatabase上のメタ情報を取得し見つからなければ再走査する
	const AssetMeta* meta = assetDataBase_.Find(sceneAsset);
	if (!meta) {

		assetDataBase_.RebuildMeta();
		meta = assetDataBase_.Find(sceneAsset);
	}
	if (!meta || meta->type != AssetType::Scene) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: requested asset is not a scene.");
		return false;
	}

	// 実ファイルが存在するシーンだけ開く
	const std::filesystem::path fullPath = assetDataBase_.ResolveFullPath(sceneAsset);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: scene file was not found. path={}", meta->assetPath);
		return false;
	}

	// 既存のEditWorldを空にしてから、新しいシーンツリーをロードする
	scheduler_.DetachCurrentWorld(systemContext_);
	editScenes_.UnloadAll(worldManager_.GetEditWorld());

	// アクティブシーン情報を先に差し替える
	activeScene_ = sceneAsset;
	activeScenePath_ = meta->assetPath;

	// SceneSystemを通してEntity/Componentを復元する
	if (!editScenes_.LoadSceneTree(assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to open scene. path={}", activeScenePath_);
		return false;
	}

	// シーン切り替え直後の大きな処理でdeltaTimeが跳ねないようにする
	requestFrameDeltaReset_ = true;
	if constexpr (BuildConfig::kEditorEnabled) {

		// 選択状態やUndo履歴は新しいシーンへ持ち越さない
		editorManager_.ResetSceneEditingState();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: opened scene. path={}", activeScenePath_);
	return true;
}

bool Engine::EngineApplication::SaveActiveEditScene() {

	// Active SceneInstanceの所有Entityをシーンファイルへ保存する
	if (!editScenes_.SaveActive(assetDataBase_, sceneSystem_, worldManager_.GetEditWorld())) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: failed to save active scene.");
		return false;
	}

	// 保存で.metaやAsset情報が変わる可能性があるため再走査する
	assetDataBase_.RebuildMeta();
	if constexpr (BuildConfig::kEditorEnabled) {

		// Editor上の未保存フラグを落とす
		editorManager_.MarkActiveSceneSaved();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: saved active scene. path={}", activeScenePath_);
	return true;
}

void Engine::EngineApplication::AcceptCloseRequest(bool destroyWindow) {

	SaveActiveSceneConfig();
	shutdownAccepted_ = true;
	closeRequestPending_ = false;

	if (destroyWindow) {
		WinApp::RequestCloseWindow();
	}
}

void Engine::EngineApplication::HandleCloseRequestResult() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	} else {

		if (!closeRequestPending_) {
			return;
		}

		const EditorUnsavedScenePopupResult result = editorManager_.ConsumeCloseUnsavedScenePopupResult();
		switch (result) {
		case EditorUnsavedScenePopupResult::Save:
			if (SaveActiveEditScene()) {
				AcceptCloseRequest(true);
			} else {
				closeRequestPending_ = false;
			}
			break;
		case EditorUnsavedScenePopupResult::DontSave:
			AcceptCloseRequest(true);
			break;
		case EditorUnsavedScenePopupResult::Cancel:
			closeRequestPending_ = false;
			break;
		case EditorUnsavedScenePopupResult::None:
		default:
			break;
		}
	}
}

bool Engine::EngineApplication::RequestClose() {

	if (shutdownAccepted_) {
		return true;
	}

	if constexpr (!BuildConfig::kEditorEnabled) {

		AcceptCloseRequest(false);
		return true;
	} else {

		if (!editorManager_.IsActiveSceneDirty()) {
			AcceptCloseRequest(false);
			return true;
		}

		// WM_CLOSE中にはImGuiを描画できないため、次のEditorフレームでモーダルを開く
		if (!closeRequestPending_) {
			closeRequestPending_ = true;
			editorManager_.RequestCloseUnsavedScenePopup();
		}
		return false;
	}
}

void Engine::EngineApplication::NotifyAssertBeforeAbort() {

	if (handlingAssertAbort_) {
		return;
	}

	handlingAssertAbort_ = true;
	if constexpr (BuildConfig::kEditorEnabled) {

		// Assert停止直前はImGuiの入力待ちができないため、未保存なら落ちる前に保存しておく
		if (editorManager_.IsActiveSceneDirty()) {
			SaveActiveEditScene();
		}
	}
	SaveActiveSceneConfig();
	handlingAssertAbort_ = false;
}

void Engine::EngineApplication::Finalize() {

	if (!shutdownAccepted_) {

		// WM_CLOSE以外の終了経路でも、最後に開いていたシーンだけは残す
		SaveActiveSceneConfig();
		shutdownAccepted_ = true;
	}
	WinApp::SetCloseRequestCallback(nullptr);
	Assert::SetPreAssertHandler(nullptr);

	// アセット監視スレッドを止めてから他のリソースを解放する
	assetWatchService_.Stop();

	// 終了時点のWorldに合わせてSystemContextを更新してから切り離す
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	if (worldManager_.IsPlaying()) {
		StopPlayWorld();
	} else {
		scheduler_.DetachCurrentWorld(systemContext_);
	}

	// ランタイム管理クラスを描画パイプラインより先に終了する
	skinnedAnimationManager_.Finalize();

	// GPUリソースを持つ描画パイプラインを解放する
	renderPipeline_->Finalize();
	renderPipeline_.reset();

	if constexpr (BuildConfig::kEditorEnabled) {

		editorManager_.Finalize();
	}

	// ツールが持つGPUリソースをGraphicsCore終了前に確実に解放する
	ToolRegistry::GetInstance().Clear();

	if constexpr (BuildConfig::kEditorEnabled) {

		// Editモードのbuild/reloadサービスを停止し、実行中の子プロセスを安全に回収する
		scriptBuildService_.Shutdown();
	}
	// EditWorldの登録を解除してからC#ホストを解放する
	ManagedWorldRegistry::GetInstance().Unregister(
		ManagedWorldRegistry::GetInstance().TryGetHandle(worldManager_.GetEditWorld()));
	// C#ホストと読み込んだアセンブリを解放する
	ManagedScriptRuntime::GetInstance().Finalize();

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// デバッグライン描画リソースを解放する
	LineRenderer::GetInstance()->Finalize();
#endif
}
