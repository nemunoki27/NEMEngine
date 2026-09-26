#include "GameApplication.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>

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
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Runtime/Application/RuntimeSystemRegistration.h>
#include <Engine/Core/Runtime/Application/ApplicationPreloader.h>
#include <Engine/Core/Runtime/Application/ApplicationSceneSettings.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>

// c++
#include <algorithm>
#include <chrono>

namespace {

	constexpr const char* kFrameRateConfigPath = Engine::ConfigPaths::kFrameRate;
	constexpr const char* kDefaultMaterialConfigPath =
		"GameAssets/Materials/Config/defaultMaterials.materialSettings.json";

	bool RequestGameApplicationClose() {

		return true;
	}

	void NotifyGameApplicationAssert() {

		Engine::Logger::Flush(Engine::LogType::Engine);
	}
}

void Engine::GameApplication::InitSystems() {

	RegisterRuntimeSystems(scheduler_);
}

void Engine::GameApplication::LoadActiveSceneConfig() {

	ApplicationSceneSettings::LoadGame(assetDatabase_, activeScene_);
}

void Engine::GameApplication::SaveActiveSceneConfig() const {

	ApplicationSceneSettings::Save(activeScene_, RuntimePaths::IsProductBuild());
}

void Engine::GameApplication::InitFirstScene() {

	if (!activeScene_ ||
		!editScenes_.LoadSceneTree(
			assetDatabase_, sceneSystem_, worldManager_.GetEditWorld(), activeScene_)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"GameApplication: 起動シーンを読み込めません");
		// 起動に失敗したWorldを更新せず終了処理へ戻す
		throw std::runtime_error("Startup scene loading failed");
	}
}

void Engine::GameApplication::Init(GraphicsCore& graphicsCore) {

	WinApp::SetCloseRequestCallback(RequestGameApplicationClose);
	Assert::SetPreAssertHandler(NotifyGameApplicationAssert);

	assetDatabase_.Init();
	assetDatabase_.RebuildMeta();
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
	managedStarted_ = true;
	ManagedScriptRuntime::GetInstance().Init();
	ManagedWorldRegistry::GetInstance().Register(worldManager_.GetEditWorld());
	InitSystems();

	renderPipeline_ = std::make_unique<RenderPipelineRunner>();
	renderPipeline_->Init();

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	debugDrawingStarted_ = true;
	LineRenderer::GetInstance()->Init(graphicsCore);
#endif

	systemContext_.engineContext = &graphicsCore.GetContext();
	systemContext_.graphicsPlatform = &graphicsCore.GetDXObject();
	systemContext_.assetDatabase = &assetDatabase_;
	systemContext_.skinnedAnimationManager = &skinnedAnimationManager_;
	systemContext_.animationClipManager = &animationClipManager_;
	systemContext_.runtimeWorldBaker = &runtimeWorldBaker_;
	StartPlayWorld();
	PreloadReleaseResources(graphicsCore);
	initializationComplete_ = true;
}

void Engine::GameApplication::StartPlayWorld() {

	nlohmann::json snapshot = editScenes_.SerializeSnapshot(sceneSystem_, worldManager_.GetEditWorld());

	worldManager_.CreatePlayWorld();
	if (!worldManager_.GetPlayWorld() ||
		!playScenes_.LoadSnapshot(assetDatabase_, sceneSystem_, *worldManager_.GetPlayWorld(), snapshot)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"GameApplication: 起動シーンのSnapshot読み込みに失敗しました");
		playScenes_ = SceneInstanceManager{};
		worldManager_.DestroyPlayWorld();
		return;
	}

	// 最初の更新前に全EntityのRuntime派生データを構築する
	runtimeWorldBaker_.Attach(*worldManager_.GetPlayWorld(), &assetDatabase_);
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
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
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
		services.assetDatabase = &assetDatabase_;
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
	systemContext_.assetDatabase = &assetDatabase_;
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
	request.assetDatabase = &assetDatabase_;
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



void Engine::GameApplication::PreloadReleaseResources(GraphicsCore& graphicsCore) {

	ApplicationPreloadContext context{ assetDatabase_, sceneSystem_, *renderPipeline_, skinnedAnimationManager_,
		animationClipManager_, systemContext_, worldManager_, playScenes_, runtimeWorldBaker_, activeScene_,
		[this]() { RefreshActiveWorldContext(); } };
	if (ApplicationPreloader::Run(graphicsCore, context, false)) {
		requestFrameDeltaReset_ = true;
		playWorldJustStarted_ = true;
	}
}

void Engine::GameApplication::Finalize() {

	WinApp::SetCloseRequestCallback(nullptr);
	Assert::SetPreAssertHandler(nullptr);
	if (initializationComplete_) {
		SaveActiveSceneConfig();
	}
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

	if (managedStarted_) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(worldManager_.GetEditWorld()));
	}
	if (managedStarted_) {
		ManagedScriptRuntime::GetInstance().Finalize();
		managedStarted_ = false;
	}

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (debugDrawingStarted_) {
		LineRenderer::GetInstance()->Finalize();
		debugDrawingStarted_ = false;
	}
#endif
}
