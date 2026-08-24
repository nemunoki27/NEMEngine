#include "GameApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentSystem.h>
#include <Engine/Core/World/Systems/Animation/SkinnedAnimationSystem.h>
#include <Engine/Core/World/Systems/Audio/AudioSourceSystem.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraControllerSystem.h>
#include <Engine/Core/World/Systems/Camera/CameraShakeSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>
#include <Engine/Core/World/Systems/Rendering/FlipbookAnimationSystem.h>
#include <Engine/Core/World/Systems/Rendering/UVTransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/UI/UICanvasSystem.h>
#include <Engine/Core/World/Systems/UI/UIInputSystem.h>

// c++
#include <algorithm>
#include <chrono>

namespace {

	constexpr const char* kStartupSceneConfigPath = Engine::ConfigPaths::kStartupScene;
	constexpr const char* kFrameRateConfigPath = Engine::ConfigPaths::kFrameRate;
	constexpr const char* kDefaultMaterialConfigPath = "GameAssets/Materials/Config/defaultMaterials.materialSettings.json";

	bool RequestGameApplicationClose() {

		return true;
	}

	void NotifyGameApplicationAssert() {

		Engine::Logger::Flush(Engine::LogType::Engine);
	}
}

void Engine::GameApplication::InitSystems() {

	int32_t order = 0;
	scheduler_.AddSystem(std::make_unique<HierarchySystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<UIInputSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<BehaviorSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<PhysicsSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<AudioSourceSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraControllerSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CameraShakeSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<TransformSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<ParticleSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<CollisionSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<FlipbookAnimationSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<UVTransformSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<SkinnedAnimationSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<JointAttachmentSystem>(), ++order);
	scheduler_.AddSystem(std::make_unique<UICanvasSystem>(), ++order);
}

void Engine::GameApplication::LoadActiveSceneConfig() {

	const std::filesystem::path configPath = RuntimePaths::GetProjectSettingsPath(kStartupSceneConfigPath);
	if (!JsonAdapter::Check(configPath, false)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(configPath, false);
	if (!data.is_object()) {
		return;
	}

	AssetID sceneAsset = ParseAssetReference(data, "activeScene", &assetDataBase_, AssetType::Scene);
	if (!sceneAsset) {
		return;
	}

	const std::filesystem::path fullPath = assetDataBase_.ResolveFullPath(sceneAsset);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"GameApplication: 設定が存在しないアクティブシーンを参照しています GUID={}", ToString(sceneAsset));
		return;
	}
	activeScene_ = sceneAsset;
}

void Engine::GameApplication::SaveActiveSceneConfig() const {

	nlohmann::json data = nlohmann::json::object();
	data["activeScene"] = ToAssetReferenceJson(activeScene_);
	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(ConfigPaths::kActiveScene), data);
}

void Engine::GameApplication::InitFirstScene() {

	if (!activeScene_ ||
		!editScenes_.LoadSceneTree(
			assetDataBase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"GameApplication: 起動シーンを読み込めません");
	}
}

void Engine::GameApplication::Init(GraphicsCore& graphicsCore) {

	WinApp::SetCloseRequestCallback(RequestGameApplicationClose);
	Assert::SetPreAssertHandler(NotifyGameApplicationAssert);

	assetDataBase_.Init();
	assetDataBase_.RebuildMeta();
	LoadActiveSceneConfig();

	FrameRateSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetProjectSettingsPath(kFrameRateConfigPath)));
	FrameRateSettings::GetInstance().SetUseEditorTargetFps(false);
	DefaultMaterialSettings::GetInstance().Load(
		Algorithm::PathToUTF8(RuntimePaths::GetGameRoot() / kDefaultMaterialConfigPath));
	RegisterBuiltinAnimationProperties();

	skinnedAnimationManager_.Init();
	Audio::GetInstance()->Init();

	InitFirstScene();
	ManagedScriptRuntime::GetInstance().Init();
	ManagedWorldRegistry::GetInstance().Register(worldManager_.GetEditWorld());
	InitSystems();

	renderPipeline_ = std::make_unique<RenderPipelineRunner>();
	renderPipeline_->Init();

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	LineRenderer::GetInstance()->Init(graphicsCore);
#endif

	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	systemContext_.runtimeWorldBaker = &runtimeWorldBaker_;
	StartPlayWorld();
	PreloadReleaseResources(graphicsCore);
}

void Engine::GameApplication::StartPlayWorld() {

	nlohmann::json snapshot = editScenes_.SerializeSnapshot(sceneSystem_, worldManager_.GetEditWorld());

	worldManager_.CreatePlayWorld();
	if (!worldManager_.GetPlayWorld() ||
		!playScenes_.LoadSnapshot(assetDataBase_, sceneSystem_, *worldManager_.GetPlayWorld(), snapshot)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"GameApplication: 起動シーンのSnapshot読み込みに失敗しました");
		playScenes_ = SceneInstanceManager{};
		worldManager_.DestroyPlayWorld();
		return;
	}

	// 最初の更新前に全EntityのRuntime派生データを構築する
	runtimeWorldBaker_.Attach(*worldManager_.GetPlayWorld(), &assetDataBase_);
	runtimeWorldBaker_.BakeAll();
	ManagedWorldRegistry::GetInstance().Register(*worldManager_.GetPlayWorld());
	ManagedScriptRuntime::BeginPlayTime(worldManager_.GetPlayWorld());
	systemContext_.time = 0.0f;
	systemContext_.unscaledTime = 0.0f;
	systemContext_.smoothDeltaTime = 0.0f;
	requestFrameDeltaReset_ = true;
	playWorldJustStarted_ = true;
	RefreshActiveWorldContext();
}

void Engine::GameApplication::StopPlayWorld() {

	scheduler_.DetachCurrentWorld(systemContext_);
	runtimeWorldBaker_.Detach();
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(*playWorld));
	}
	worldManager_.DestroyPlayWorld();
	playScenes_ = SceneInstanceManager{};
	(void)ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest();
	RefreshActiveWorldContext();
}

const Engine::SceneHeader* Engine::GameApplication::GetActiveSceneHeader() const {

	const SceneInstance* instance = worldManager_.IsPlaying() ? playScenes_.GetActive() : editScenes_.GetActive();
	return instance ? &instance->header : nullptr;
}

void Engine::GameApplication::RefreshActiveWorldContext() {

	ECSWorld* world = worldManager_.IsPlaying() ? worldManager_.GetPlayWorld() : &worldManager_.GetEditWorld();
	SceneInstanceManager& scenes = worldManager_.IsPlaying() ? playScenes_ : editScenes_;
	systemContext_.mode = worldManager_.IsPlaying() ? WorldMode::Play : WorldMode::Edit;
	systemContext_.world = world;
	systemContext_.activeSceneHeader = GetActiveSceneHeader();

	if (world) {
		WorldCommandServices services{};
		services.assetDatabase = &assetDataBase_;
		services.sceneInstances = &scenes;
		services.sceneSystem = &sceneSystem_;
		world->SetCommandServices(services);
	}
	CollisionSettings::GetInstance().BindGlobal();
}

bool Engine::GameApplication::HandleApplicationQuitRequest() {

	if (!ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest()) {
		return false;
	}

	Logger::Output(LogType::Engine, spdlog::level::info,
		"GameApplication: Application.Quit要求を受けたため終了します");
	WinApp::RequestCloseWindow();
	return true;
}

void Engine::GameApplication::Tick(GraphicsCore& graphicsCore, float deltaTime) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	LineRenderer::GetInstance()->BeginFrame();
#endif

	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	RefreshActiveWorldContext();

	const bool skipFirstAdvance = playWorldJustStarted_;
	playWorldJustStarted_ = false;
	const float rawDelta = skipFirstAdvance ? 0.0f : deltaTime;
	systemContext_.deltaTime = ManagedScriptRuntime::AdvanceTime(
		rawDelta, systemContext_.fixedDeltaTime, worldManager_.IsPlaying() && !skipFirstAdvance);
	systemContext_.unscaledDeltaTime = rawDelta;
	const float timeDelta = worldManager_.IsPlaying() ?
		systemContext_.deltaTime : rawDelta;
	systemContext_.time += timeDelta;
	systemContext_.unscaledTime += rawDelta;
	if (0.0f < rawDelta) {
		const float smoothWeight =
			(std::clamp)(rawDelta * 8.0f, 0.0f, 1.0f);
		systemContext_.smoothDeltaTime =
			systemContext_.smoothDeltaTime <= 0.0f ?
				timeDelta :
				systemContext_.smoothDeltaTime +
				(timeDelta - systemContext_.smoothDeltaTime) *
				smoothWeight;
	}

	LineImmediateBuffer::GetInstance().BeginFrame();

	ECSWorld* world = systemContext_.world;
	if (world) {
		const uint64_t sceneRevision = playScenes_.GetRevision();
		const uint64_t scriptExceptionVersion = ManagedScriptExceptionStore::GetInstance().Version();

		FrameProfiler::ScopedSample ecsSample(FrameProfiler::Category::Ecs);
		scheduler_.Tick(world, systemContext_);

		if (sceneRevision != playScenes_.GetRevision()) {
			requestFrameDeltaReset_ = true;
			RefreshActiveWorldContext();
		}
		if (scriptExceptionVersion != ManagedScriptExceptionStore::GetInstance().Version()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"GameApplication: Play中にScript例外が発生しました");
		}
	}

	HandleApplicationQuitRequest();
}

bool Engine::GameApplication::ConsumeFrameDeltaResetRequest() {

	const bool requested = requestFrameDeltaReset_;
	requestFrameDeltaReset_ = false;
	return requested;
}

Engine::RenderFrameRequest Engine::GameApplication::BuildRenderFrameRequest(GraphicsCore& graphicsCore) {

	RenderFrameRequest request{};
	request.header = GetActiveSceneHeader();
	request.world = systemContext_.world;
	request.systemContext = &systemContext_;
	request.assetDatabase = &assetDataBase_;
	request.sceneInstances = worldManager_.IsPlaying() ? &playScenes_ : &editScenes_;

	if (const SceneInstance* active = request.sceneInstances->GetActive()) {
		request.activeSceneInstanceID = active->instanceID;
	}

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
	return request;
}

void Engine::GameApplication::Render(GraphicsCore& graphicsCore) {

	graphicsCore.TickFrameServices();
	graphicsCore.Render();
	renderPipeline_->Render(graphicsCore, BuildRenderFrameRequest(graphicsCore));
	renderPipeline_->PresentViewToBackBuffer(graphicsCore, RenderViewKind::Game);
}

void Engine::GameApplication::RenderPlatformWindows([[maybe_unused]] GraphicsCore& graphicsCore) {

}

void Engine::GameApplication::WarmupReleaseWorld(GraphicsCore& graphicsCore, ECSWorld& world,
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

	renderPipeline_->Render(graphicsCore, request);
	// Scene固有Bufferが破棄される前にCopy Queueを提出し、描画Queueとの依存を確定する
	graphicsCore.GetBufferUploadService().SubmitBatch();
	graphicsCore.GetDXObject().WaitForGPU();
	graphicsCore.GetBufferUploadService().FlushAndWait();
}

void Engine::GameApplication::PreloadReleaseResources(GraphicsCore& graphicsCore) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	(void)graphicsCore;
	return;
#else
	const auto startTime = std::chrono::steady_clock::now();
	Logger::Output(LogType::Engine, "[実行時事前読み込み] Release起動時の事前読み込みを開始します");

	renderPipeline_->PreloadRuntimeAssets(graphicsCore, assetDataBase_);

	std::vector<const AssetMeta*> assets;
	assets.reserve(assetDataBase_.GetAssets().size());
	for (const auto& [assetID, meta] : assetDataBase_.GetAssets()) {
		assets.emplace_back(&meta);
	}
	std::sort(assets.begin(), assets.end(), [](const AssetMeta* lhs, const AssetMeta* rhs) {
		return lhs->assetPath < rhs->assetPath;
		});

	std::vector<AssetID> sceneAssets;
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

	for (AssetID sceneAsset : sceneAssets) {
		if (sceneAsset == activeScene_) {
			continue;
		}

		ECSWorld warmupWorld{};
		SceneInstanceManager warmupScenes{};
		if (!warmupScenes.LoadSceneTree(assetDataBase_, sceneSystem_, warmupWorld, sceneAsset)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[実行時事前読み込み] シーンの読み込みに失敗しました GUID={}", ToString(sceneAsset));
			continue;
		}

		SystemContext warmupContext{};
		warmupContext.engineContext = &graphicsCore.GetContext();
		warmupContext.graphicsPlatform = &graphicsCore.GetDXObject();
		warmupContext.assetDatabase = &assetDataBase_;
		warmupContext.skinnedAnimationManager = &skinnedAnimationManager_;
		warmupContext.animationClipManager = &animationClipManager_;
		warmupContext.mode = WorldMode::Play;
		warmupContext.world = &warmupWorld;
		if (const SceneInstance* activeScene = warmupScenes.GetActive()) {
			warmupContext.activeSceneHeader = &activeScene->header;
		}

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
	}

	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.assetDatabase = &assetDataBase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	RefreshActiveWorldContext();
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		WarmupReleaseWorld(graphicsCore, *playWorld, playScenes_, systemContext_);
	}

	graphicsCore.GetTextureUploadService().WaitAll();
	graphicsCore.GetBufferUploadService().FlushAndWait();
	graphicsCore.GetDXObject().WaitForGPU();
	requestFrameDeltaReset_ = true;
	playWorldJustStarted_ = true;

	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - startTime).count();
	Logger::Output(LogType::Engine,
		"[実行時事前読み込み] Release起動時の事前読み込みが完了しました シーン数={} 経過={}ms",
		sceneAssets.size(), elapsed);
	Logger::Flush(LogType::Engine);
#endif
}

void Engine::GameApplication::Finalize() {

	WinApp::SetCloseRequestCallback(nullptr);
	Assert::SetPreAssertHandler(nullptr);
	SaveActiveSceneConfig();
	if (worldManager_.IsPlaying()) {
		StopPlayWorld();
	} else {
		scheduler_.DetachCurrentWorld(systemContext_);
	}

	skinnedAnimationManager_.Finalize();
	if (renderPipeline_) {
		renderPipeline_->Finalize();
		renderPipeline_.reset();
	}

	ManagedWorldRegistry::GetInstance().Unregister(
		ManagedWorldRegistry::GetInstance().TryGetHandle(worldManager_.GetEditWorld()));
	ManagedScriptRuntime::GetInstance().Finalize();

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	LineRenderer::GetInstance()->Finalize();
#endif
}
