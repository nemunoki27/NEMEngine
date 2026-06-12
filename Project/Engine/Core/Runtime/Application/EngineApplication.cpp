#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Rendering/Renderer/Outline/EditorSelectionOutlineRequestService.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// ECSシステム
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Rendering/UVTransformSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioSourceSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraControllerSystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>

//============================================================================
//	EngineApplication classMethods
//============================================================================
namespace {

	constexpr const char* kActiveSceneConfigPath = "Config/activeScene.exeConfig.json";
	constexpr const char* kFrameRateConfigPath = "Config/frameRate.exeConfig.json";

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
	scheduler_.AddSystem(std::make_unique<BehaviorSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<AudioSourceSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraControllerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<TransformUpdateSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CollisionSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<UVTransformUpdateSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<SkinnedAnimationUpdateSystem>(), ++order);
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
	const std::filesystem::path configPath = RuntimePaths::GetEngineAssetPath(kActiveSceneConfigPath);
	if (!JsonAdapter::Check(configPath.string(), false)) {
		return;
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

	// .exeConfig系と同じくEngine/Assets/Config配下へ小さなJSONで保存する
	nlohmann::json data = nlohmann::json::object();
	data["activeScene"] = ToAssetReferenceJson(activeScene_);

	const std::filesystem::path configPath = RuntimePaths::GetEngineAssetPath(kActiveSceneConfigPath);
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
	FrameRateSettings::GetInstance().Load(RuntimePaths::GetEngineAssetPath(kFrameRateConfigPath).string());

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
	// Editモードの非同期build/reloadサービスを初期化しsource baselineとlast-known-goodを整える
	scriptBuildService_.Initialize(&ManagedScriptRuntime::GetInstance());
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
	}
}

const Engine::SceneHeader* Engine::EngineApplication::GetActiveSceneHeader() const {

	// Play中はPlayWorld側、それ以外はEditWorld側のアクティブシーンを参照する
	const SceneInstance* instance = worldManager_.IsPlaying() ? playScenes_.GetActive() : editScenes_.GetActive();
	return instance ? &instance->header : nullptr;
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

	// アクティブなシーンインスタンスのIDを取得する
	const SceneInstance* activeInstance = worldManager_.IsPlaying() ? playScenes_.GetActive() : editScenes_.GetActive();
	// ワールドの状態に応じてシーンインスタンスのリストを切り替える
	request.sceneInstances = worldManager_.IsPlaying() ? &playScenes_ : &editScenes_;
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

	// システムコンテキストの更新
	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.deltaTime = deltaTime;
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;

	// 非同期build/reload状態機械を進める、Play中はreloadを適用せず変更検知のdirtyのみ行う
	// Editor main threadをblockしない
	scriptBuildService_.Tick(worldManager_.IsPlaying());

	// プレイモードの切り替え
	HandlePlayToggle();
	// Play中の一時停止、再開、1フレーム送りを処理する
	HandlePlayPauseRequests();
	// エディタから要求されたシーン操作
	HandleEditorSceneRequests();
	// Play/Stopでワールド状態が変わった後のモードを、このフレームのECS処理へ反映する
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	// gameplay time serviceを1フレーム進め、time scale適用後のdeltaTimeを全システムへ渡す
	// deltaTimeをscaleするとschedulerのfixed substep累積も自動的にscaleされTimeScale=0で停止する
	{
		const bool advancePlayTime = ShouldAdvanceActiveWorld() && systemContext_.mode == WorldMode::Play;
		const float rawDelta = ShouldAdvanceActiveWorld() ? deltaTime : 0.0f;
		systemContext_.deltaTime = ManagedScriptRuntime::AdvanceTime(rawDelta, systemContext_.fixedDeltaTime, advancePlayTime);
	}

	ECSWorld* world = GetActiveWorld();
	const SceneHeader* header = GetActiveSceneHeader();
	SceneInstanceManager& activeScenes = GetActiveScenes();
	const SceneInstance* activeSceneInstance = activeScenes.GetActive();

	// scripting callbackがparent無しEntity生成等で参照するactive worldをcontextへ載せる
	systemContext_.world = world;

	// Prefab/SceneのWorldCommandBufferコマンドがFlush時に参照する外部サービスをactive worldへ設定する
	// 非所有ポインタでworld切替やEdit/Play切替に追従して毎フレーム更新する
	if (world) {
		WorldCommandServices services{};
		services.assetDatabase = &assetDataBase_;
		services.sceneInstances = &activeScenes;
		services.sceneSystem = &sceneSystem_;
		world->SetCommandServices(services);
	}

	// シーンごとのCollision設定を、Editor/Play共通の現在設定へ反映する
	systemContext_.activeSceneHeader = header;
	if (header) {
		CollisionSettings::GetInstance().SetActiveSettingsAsset(header->collisionSettings, systemContext_.assetDatabase);
	} else {
		CollisionSettings::GetInstance().SetActiveSettingsAsset({}, systemContext_.assetDatabase);
	}

	if constexpr (BuildConfig::kEditorEnabled) {

		// エディタUIとツールが参照する現在の状態をまとめる
		editorContext_.isPlaying = worldManager_.IsPlaying();
		editorContext_.isPlayPaused = playPaused_;
		editorContext_.activeScenePath = activeScenePath_;
		editorContext_.activeSceneHeader = header;
		editorContext_.activeSceneAsset = activeSceneInstance ? activeSceneInstance->sceneAsset : activeScene_;
		editorContext_.activeSceneInstanceID = activeSceneInstance ? activeSceneInstance->instanceID : UUID{};
		editorContext_.sceneInstances = &activeScenes;
		editorContext_.activeWorld = world;
		// EditWorldはPlay中でも常に有効でApply Runtime Values To Authoringで参照する
		editorContext_.editWorld = &worldManager_.GetEditWorld();
		editorContext_.assetDatabase = &assetDataBase_;
		// managed scriptingのEditor向けサービス境界を公開する、read-only snapshot + request interface
		editorContext_.scriptBuildService = &scriptBuildService_;

		const bool hidePanels = editorManager_.GetLayoutState().hidePanels;
		if (hidePanels) {

			const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
			Input::GetInstance()->SetViewRect(InputViewArea::Game, Vector2(0.0f, 0.0f),
				windowSetting.engineSizeFloat, windowSetting.gameSizeFloat);
		} else {

			// C++ツールの更新でUI描画とは分離してGame側ツールも同じ経路で扱う
			ToolContext toolContext{};
			toolContext.world = world;
			toolContext.assetDatabase = &assetDataBase_;
			toolContext.systemContext = &systemContext_;
			toolContext.sceneInstances = &activeScenes;
			toolContext.activeSceneHeader = header;
			toolContext.activeSceneAsset = editorContext_.activeSceneAsset;
			toolContext.activeSceneInstanceID = editorContext_.activeSceneInstanceID;
			toolContext.activeScenePath = activeScenePath_;
			toolContext.isPlaying = worldManager_.IsPlaying();
			toolContext.canEditScene = !worldManager_.IsPlaying() && world;
			toolContext.deltaTime = deltaTime;
			ToolRegistry::GetInstance().Tick(toolContext);
		}

		// エディタのフレーム開始処理
		editorManager_.BeginFrame(graphicsCore, editorContext_);
		HandleCloseRequestResult();
	}

	// ECSシステムの更新
	if (ShouldAdvanceActiveWorld()) {

		FrameProfiler::ScopedSample ecsSample(FrameProfiler::Category::Ecs);
		scheduler_.Tick(GetActiveWorld(), systemContext_);
	}
	if (playFrameStepRequested_) {

		playFrameStepRequested_ = false;
		systemContext_.deltaTime = 0.0f;
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

		// Stop時は実行中WorldからSchedulerを切り離して、PlayWorldを破棄する
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

	// 終了時点のWorldに合わせてSystemContextを更新してから切り離す
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	scheduler_.DetachCurrentWorld(systemContext_);

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

	// Editモードのbuild/reloadサービスを停止し、実行中の子プロセスを安全に回収する
	scriptBuildService_.Shutdown();
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
