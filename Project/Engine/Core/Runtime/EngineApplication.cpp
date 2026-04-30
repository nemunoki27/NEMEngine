#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Line/LineRenderer.h>
#include <Engine/Core/Build/BuildConfig.h>
#include <Engine/Core/Collision/CollisionSettings.h>
#include <Engine/Core/Scripting/ManagedScriptRuntime.h>
#include <Engine/Core/Tools/ToolRegistry.h>
#include <Engine/Audio/Audio.h>
#include <Engine/Editor/Project/ProjectAssetFileUtility.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Input/Input.h>

// ECSシステム
#include <Engine/Core/ECS/System/Builtin/BehaviorSystem.h>
#include <Engine/Core/ECS/System/Builtin/TransformUpdateSystem.h>
#include <Engine/Core/ECS/System/Builtin/UVTransformUpdateSystem.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>
#include <Engine/Core/ECS/System/Builtin/SkinnedAnimationUpdateSystem.h>
#include <Engine/Core/ECS/System/Builtin/AudioSourceSystem.h>
#include <Engine/Core/ECS/System/Builtin/CameraControllerSystem.h>
#include <Engine/Core/ECS/System/Builtin/CollisionSystem.h>

//============================================================================
//	EngineApplication classMethods
//============================================================================

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

	// アクティブなシーンのアセットIDを取得
	activeScene_ = assetDataBase_.ImportOrGet(activeScenePath_, AssetType::Scene);
	// シーンをロードしてエディタワールドにインスタンスを作成
	editScenes_.LoadSceneTree(assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_);
}

void Engine::EngineApplication::Init(GraphicsCore& graphicsCore) {

	// アセットデータベース初期化
	assetDataBase_.Init();
	assetDataBase_.RebuildMeta();

	// 骨アニメーション管理の初期化
	skinnedAnimationManager_.Init();
	// Audio管理の初期化
	Audio::GetInstance()->Init();

	// 最初のシーンを作成
	InitFirstScene();
	// C#スクリプトランタイム初期化
	ManagedScriptRuntime::GetInstance().Init();
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
		showGameView = layout.showGameView;
		showSceneView = layout.showSceneView;
		sceneViewCameraSelection = editorManager_.GetSceneViewCameraSelection();
		manualSceneCamera = editorManager_.GetSceneViewCameraState();
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
#endif

	// システムコンテキストの更新
	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.deltaTime = deltaTime;
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;

	// Play中はホットリロードしない。Edit中のみC#変更を自動検知して再ビルド・再ロードする
	if (!worldManager_.IsPlaying()) {
		ManagedScriptRuntime::GetInstance().AutoRebuildOnScriptChanges();
	}

	// プレイモードの切り替え
	HandlePlayToggle();
	// エディタから要求されたシーン操作
	HandleEditorSceneRequests();
	// Play/Stopでワールド状態が変わった後のモードを、このフレームのECS処理へ反映する
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;

	ECSWorld* world = GetActiveWorld();
	const SceneHeader* header = GetActiveSceneHeader();
	SceneInstanceManager& activeScenes = GetActiveScenes();
	const SceneInstance* activeSceneInstance = activeScenes.GetActive();

	// シーンごとのCollision設定を、Editor/Play共通の現在設定へ反映する
	systemContext_.activeSceneHeader = header;
	if (header) {
		CollisionSettings::GetInstance().SetActiveSettingsAssetPath(header->collisionSettingsPath);
	} else {
		CollisionSettings::GetInstance().SetActiveSettingsAssetPath("");
	}

	if constexpr (BuildConfig::kEditorEnabled) {

		// エディタUIとツールが参照する現在の状態をまとめる
		editorContext_.isPlaying = worldManager_.IsPlaying();
		editorContext_.activeScenePath = activeScenePath_;
		editorContext_.activeSceneHeader = header;
		editorContext_.activeSceneAsset = activeSceneInstance ? activeSceneInstance->sceneAsset : activeScene_;
		editorContext_.activeSceneInstanceID = activeSceneInstance ? activeSceneInstance->instanceID : UUID{};
		editorContext_.activeWorld = world;
		editorContext_.assetDatabase = &assetDataBase_;

		// C++ツールの更新。UI描画とは分離して、Game側ツールも同じ経路で扱う
		ToolContext toolContext{};
		toolContext.world = world;
		toolContext.assetDatabase = &assetDataBase_;
		toolContext.systemContext = &systemContext_;
		toolContext.activeSceneHeader = header;
		toolContext.activeSceneAsset = editorContext_.activeSceneAsset;
		toolContext.activeSceneInstanceID = editorContext_.activeSceneInstanceID;
		toolContext.activeScenePath = activeScenePath_;
		toolContext.isPlaying = worldManager_.IsPlaying();
		toolContext.canEditScene = !worldManager_.IsPlaying() && world;
		toolContext.deltaTime = deltaTime;
		ToolRegistry::GetInstance().Tick(toolContext);

		// エディタのフレーム開始処理
		editorManager_.BeginFrame(graphicsCore, editorContext_);
	}

	// ECSシステムの更新
	scheduler_.Tick(GetActiveWorld(), systemContext_);
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

	// ワールドを描画
	renderPipeline_->Render(graphicsCore, BuildRenderFrameRequest(graphicsCore, GetActiveWorld(), GetActiveSceneHeader()));

	if constexpr (BuildConfig::kEditorEnabled) {

		// シーンビューのメッシュピック処理
		editorManager_.ExecuteSceneMeshPicking(graphicsCore, editorContext_, *renderPipeline_);

		// エディタのフレーム終了処理
		editorManager_.EndFrame(graphicsCore, editorContext_, &renderPipeline_->GetViewportRenderService(),
			&renderPipeline_->GetResolvedView(RenderViewKind::Scene));
	} else {

		// エディタがない場合はゲームビューをバックバッファに描画する
		renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
	}
}

void Engine::EngineApplication::HandlePlayToggle() {

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

		// プレイ開始前にC#スクリプトをビルドし、最新のDLLから型情報を反映する
		auto& scriptRuntime = ManagedScriptRuntime::GetInstance();
		bool waitForManagedDebuggerOnPlay = false;
		if constexpr (BuildConfig::kEditorEnabled) {
			waitForManagedDebuggerOnPlay = editorManager_.GetLayoutState().waitForManagedDebuggerOnPlay;
		}
		scriptRuntime.UnloadGameAssembly();
		if (!scriptRuntime.BuildGameAssembly()) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"EngineApplication: failed to enter Play mode. C# script build failed.");
			return;
		}
		if (!scriptRuntime.ReloadGameAssembly(waitForManagedDebuggerOnPlay)) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: GameScripts.dll was not loaded. Play mode will start without managed scripts.");
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
			return;
		}
	} else {

		// Stop時は実行中WorldからSchedulerを切り離して、PlayWorldを破棄する
		scheduler_.DetachCurrentWorld(systemContext_);
		worldManager_.DestroyPlayWorld();
		playScenes_ = SceneInstanceManager{};
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

	// AssetDatabase上のメタ情報を取得する。見つからなければ再走査する
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

void Engine::EngineApplication::Finalize() {

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

	// C#ホストと読み込んだアセンブリを解放する
	ManagedScriptRuntime::GetInstance().Finalize();

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// デバッグライン描画リソースを解放する
	LineRenderer::GetInstance()->Finalize();
#endif
}
