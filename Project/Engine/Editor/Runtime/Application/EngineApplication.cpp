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
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
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
#include <Engine/Core/World/Systems/UI/IrisTransitionSystem.h>
#include <Engine/Core/World/Systems/UI/UICanvasSystem.h>
// c++
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Animation/AnimationPlayerSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioSourceSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraControllerSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraShakeSystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>

//============================================================================
//	EngineApplication classMethods
//============================================================================
namespace {

	constexpr const char* kActiveSceneConfigPath = Engine::ConfigPaths::kActiveScene;
	constexpr const char* kStartupSceneConfigPath = Engine::ConfigPaths::kStartupScene;
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
	// C#からの遷移要求を同フレームで描画状態へ反映する
	scheduler_.AddSystem(std::make_unique<IrisTransitionSystem>(), ++order);
	// プロパティアニメはスクリプトの後で適用し、LateUpdateのTransform確定前に値を書く
	scheduler_.AddSystem(std::make_unique<AnimationPlayerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<PhysicsSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<AudioSourceSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraControllerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraShakeSystem>(), ++order);
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

	if (!activeScene_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: active scene is not configured");
		return;
	}
	// アクティブなシーンの表示・保存用パスはGUIDから引き直す
	if (const AssetMeta* meta = assetDataBase_.Find(activeScene_)) {
		activeScenePath_ = meta->assetPath;
	}
	// シーンをロードしてエディタワールドにインスタンスを作成
	if (!editScenes_.LoadSceneTree(
		assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: active scene could not be loaded. guid={}",
			ToString(activeScene_));
	}
}

void Engine::EngineApplication::LoadActiveSceneConfig() {

	const auto loadSceneConfig = [&](const std::filesystem::path& configPath) {

		if (!JsonAdapter::Check(configPath, false)) {
			return;
		}
		const nlohmann::json data = JsonAdapter::Load(configPath, false);
		if (!data.is_object()) {
			return;
		}

		const AssetID sceneAsset =
			ParseAssetReference(data, "activeScene", &assetDataBase_, AssetType::Scene);
		const std::filesystem::path fullPath =
			assetDataBase_.ResolveFullPath(sceneAsset);
		if (!sceneAsset || fullPath.empty() || !std::filesystem::exists(fullPath)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: scene config points missing scene. config={}",
				Algorithm::PathToUTF8(configPath));
			return;
		}
		activeScene_ = sceneAsset;
		if (const AssetMeta* meta = assetDataBase_.Find(sceneAsset)) {
			activeScenePath_ = meta->assetPath;
		}
		};

	// 共有の起動シーンを基準にし、ユーザーが最後に開いていたシーンがあれば上書きする
	loadSceneConfig(RuntimePaths::GetProjectSettingsPath(kStartupSceneConfigPath));
	loadSceneConfig(RuntimePaths::GetUserSettingsPath(kActiveSceneConfigPath));
}

void Engine::EngineApplication::SaveActiveSceneConfig() const {

	// SDK更新で消えないよう、ゲームルート配下のConfigへ小さなJSONで保存する
	nlohmann::json data = nlohmann::json::object();
	data["activeScene"] = ToAssetReferenceJson(activeScene_);

	const std::filesystem::path configPath = RuntimePaths::GetUserSettingsPath(kActiveSceneConfigPath);
	JsonAdapter::Save(configPath, data);
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
	FrameRateSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetProjectSettingsPath(kFrameRateConfigPath)));
	// 描画タイプごとのデフォルトマテリアル設定をGameAssets配下から読み込む
	DefaultMaterialSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetGameRoot() / kDefaultMaterialConfigPath));
	// AnimationClipの評価に必要なPropertyをEditorの有無に関係なく登録する
	RegisterBuiltinAnimationProperties();

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
		PreloadReleaseResources(graphicsCore);
	}
}

void Engine::EngineApplication::PreloadReleaseResources(GraphicsCore& graphicsCore) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	(void)graphicsCore;
	return;
#else
	const auto startTime = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, "[RuntimePreload] Release startup preload begin");

	// ファイル単位で列挙できる描画アセットとPSOを先に作成する
	renderPipeline_->PreloadRuntimeAssets(graphicsCore, assetDataBase_);

	std::vector<const AssetMeta*> assets{};
	assets.reserve(assetDataBase_.GetAssets().size());
	for (const auto& [assetID, meta] : assetDataBase_.GetAssets()) {
		assets.emplace_back(&meta);
	}
	std::sort(assets.begin(), assets.end(), [](const AssetMeta* lhs, const AssetMeta* rhs) {
		return lhs->assetPath < rhs->assetPath;
		});

	std::vector<AssetID> sceneAssets{};
	for (const AssetMeta* meta : assets) {

		switch (meta->type) {
		case AssetType::Mesh:
			skinnedAnimationManager_.RequestLoadAsync(assetDataBase_, meta->guid);
			break;
		case AssetType::AnimationClip:
			animationClipManager_.GetOrLoad(assetDataBase_, meta->guid);
			break;
		case AssetType::Audio:
		{
			const std::filesystem::path fullPath = assetDataBase_.ResolveFullPath(meta->guid);
			if (!fullPath.empty()) {
				Audio::GetInstance()->EnsureLoaded(fullPath);
			}
			break;
		}
		case AssetType::Scene:
			if (meta->assetPath.starts_with("GameAssets/")) {
				sceneAssets.emplace_back(meta->guid);
			}
			break;
		default:
			break;
		}
	}
	skinnedAnimationManager_.WaitAll();

	// 各シーンを一時ワールドへ展開し、ECS更新を行わず描画リソースだけ作成する
	for (AssetID sceneAsset : sceneAssets) {

		if (sceneAsset == activeScene_) {
			continue;
		}
		const AssetMeta* sceneMeta = assetDataBase_.Find(sceneAsset);
		Logger::Output(LogType::Engine, "[RuntimePreload] Scene warmup begin. path={}",
			sceneMeta ? sceneMeta->assetPath : ToString(sceneAsset));
		ECSWorld warmupWorld{};
		SceneInstanceManager warmupScenes{};
		if (!warmupScenes.LoadSceneTree(assetDataBase_, sceneSystem_, warmupWorld, sceneAsset)) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[RuntimePreload] Scene load failed. guid={}", ToString(sceneAsset));
			continue;
		}

		SystemContext warmupContext{};
		warmupContext.engineContext = &graphicsCore.GetContext();
		warmupContext.graphicsPlatform = &graphicsCore.GetDXObject();
		warmupContext.assetDatabase = &assetDataBase_;
		warmupContext.skinnedAnimationManager = &skinnedAnimationManager_;
		warmupContext.animationClipManager = &animationClipManager_;
		warmupContext.world = &warmupWorld;
		if (const SceneInstance* activeScene = warmupScenes.GetActive()) {
			warmupContext.activeSceneHeader = &activeScene->header;
		}
		warmupContext.mode = WorldMode::Play;

		WorldCommandServices services{};
		services.assetDatabase = &assetDataBase_;
		services.sceneInstances = &warmupScenes;
		services.sceneSystem = &sceneSystem_;
		warmupWorld.SetCommandServices(services);

		HierarchySystem hierarchySystem{};
		hierarchySystem.OnWorldEnter(warmupWorld, warmupContext);
		TransformSystem transformSystem{};
		transformSystem.LateUpdate(warmupWorld, warmupContext);
		WarmupReleaseWorld(graphicsCore, warmupWorld, warmupScenes, warmupContext);
		Logger::Output(LogType::Engine, "[RuntimePreload] Scene warmup completed. path={}",
			sceneMeta ? sceneMeta->assetPath : ToString(sceneAsset));
	}

	// 最後に実際の開始シーンを描画し、カメラとPostProcessの共有状態も開始シーンへ戻す
	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	systemContext_.deltaTime = 0.0f;
	systemContext_.unscaledDeltaTime = 0.0f;
	RefreshActiveWorldContext();
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		Logger::Output(LogType::Engine, "[RuntimePreload] Startup scene warmup begin");
		WarmupReleaseWorld(graphicsCore, *playWorld, playScenes_, systemContext_);
		Logger::Output(LogType::Engine, "[RuntimePreload] Startup scene warmup completed");
	}

	graphicsCore.GetTextureUploadService().WaitAll();
	graphicsCore.GetBufferUploadService().FlushAndWait();
	graphicsCore.GetDXObject().WaitForGPU();
	requestFrameDeltaReset_ = true;
	playWorldJustStarted_ = true;

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - startTime).count();
	Logger::Output(LogType::Engine,
		"[RuntimePreload] Release startup preload completed. Scenes={} Elapsed={}ms",
		sceneAssets.size(), elapsed);
	Logger::Flush(LogType::Engine);
#endif
}

void Engine::EngineApplication::WarmupReleaseWorld(GraphicsCore& graphicsCore, ECSWorld& world,
	SceneInstanceManager& scenes, SystemContext& context) {

	const SceneInstance* activeScene = scenes.GetActive();
	if (!activeScene) {
		return;
	}
	context.world = &world;
	context.activeSceneHeader = &activeScene->header;
	context.deltaTime = 0.0f;
	context.unscaledDeltaTime = 0.0f;

	RenderFrameRequest request{};
	request.sceneInstances = &scenes;
	request.header = &activeScene->header;
	request.activeSceneInstanceID = activeScene->instanceID;
	request.world = &world;
	request.systemContext = &context;
	request.assetDatabase = &assetDataBase_;

	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	RenderViewRequest& gameView = request.views[static_cast<uint32_t>(RenderViewKind::Game)];
	gameView.kind = RenderViewKind::Game;
	gameView.enabled = true;
	gameView.width = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.x));
	gameView.height = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.y));
	gameView.sourceKind = RenderViewSourceKind::WorldCamera;

	RenderViewRequest& sceneView = request.views[static_cast<uint32_t>(RenderViewKind::Scene)];
	sceneView.kind = RenderViewKind::Scene;
	sceneView.enabled = false;
	sceneView.width = 0;
	sceneView.height = 0;

	Logger::Output(LogType::Engine, "[RuntimePreload] Scene render recording begin");
	renderPipeline_->Render(graphicsCore, request);
	Logger::Output(LogType::Engine, "[RuntimePreload] Scene render recording completed");
	Logger::Output(LogType::Engine, "[RuntimePreload] Scene GPU wait begin");
	graphicsCore.GetDXObject().WaitForGPU();
	Logger::Output(LogType::Engine, "[RuntimePreload] Scene GPU wait completed");
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
			const DXGI_SWAP_CHAIN_DESC1& swapChainDesc = graphicsCore.GetSwapChainDesc();
			Input::GetInstance()->SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f),
				Vector2(static_cast<float>(swapChainDesc.Width), static_cast<float>(swapChainDesc.Height)),
				windowSetting.gameSizeFloat);
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
		const uint64_t sceneRevisionBeforeTick = playingThisTick ?
			playScenes_.GetRevision() : 0;
		const uint64_t scriptExceptionVersion = playingThisTick ?
			ManagedScriptExceptionStore::GetInstance().Version() : 0;
		scheduler_.Tick(GetActiveWorld(), systemContext_);
		if (playingThisTick &&
			sceneRevisionBeforeTick != playScenes_.GetRevision()) {

			// 同期シーン読み込みに使った時間を次のPlayフレームへ持ち越さない
			requestFrameDeltaReset_ = true;
		}
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
			// 読み込み中の全シーンの保存に成功した場合だけ新規シーン作成へ進む
			if (SaveAllEditScenes()) {
				CreateNewEditScene();
			}
			break;
		case EditorSceneRequestType::SaveAndOpenScene:
			// 読み込み中の全シーンの保存に成功した場合だけ別シーンを開く
			if (SaveAllEditScenes()) {
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
		editorManager_.ResetSceneDirtyState();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: opened scene. path={}", activeScenePath_);
	return true;
}

bool Engine::EngineApplication::SaveActiveEditScene() {

	const SceneInstance* activeScene = editScenes_.GetActive();
	const AssetID sceneAsset = activeScene ? activeScene->sceneAsset : AssetID{};

	// Active SceneInstanceの所有Entityをシーンファイルへ保存する
	if (!editScenes_.SaveActive(assetDataBase_, sceneSystem_, worldManager_.GetEditWorld())) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: failed to save active scene.");
		return false;
	}

	// 保存で.metaやAsset情報が変わる可能性があるため再走査する
	assetDataBase_.RebuildMeta();
	if constexpr (BuildConfig::kEditorEnabled) {

		// 保存したシーンだけ未保存状態を落とす
		editorManager_.MarkSceneSaved(sceneAsset);
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: saved active scene. path={}", activeScenePath_);
	return true;
}

bool Engine::EngineApplication::SaveAllEditScenes() {

	std::unordered_set<AssetID> savedAssets;
	for (const SceneInstance& scene : editScenes_.GetAll()) {

		if (!scene.sceneAsset || !savedAssets.insert(scene.sceneAsset).second) {
			continue;
		}
		if (!editScenes_.Save(
			assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), scene.sceneAsset)) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: failed to save scene. asset={}", ToString(scene.sceneAsset));
			return false;
		}
	}

	assetDataBase_.RebuildMeta();
	if constexpr (BuildConfig::kEditorEnabled) {
		editorManager_.MarkAllScenesSaved();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: saved loaded scenes. count={}", savedAssets.size());
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
			if (SaveAllEditScenes()) {
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

		if (!editorManager_.HasDirtyScenes()) {
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
		if (editorManager_.HasDirtyScenes()) {
			SaveAllEditScenes();
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

int Engine::RunEditorApplication() {

	Framework framework(std::make_unique<EngineApplication>());
	framework.Run();
	return 0;
}
