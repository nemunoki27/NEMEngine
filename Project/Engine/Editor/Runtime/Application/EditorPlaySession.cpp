#include "EditorPlaySession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>

using namespace Engine;

Engine::EditorPlaySession::EditorPlaySession(AssetDatabase& assetDatabase,
	WorldManager& worldManager,
	SceneInstanceManager& editScenes,
	SceneInstanceManager& playScenes,
	SceneSystem& sceneSystem,
	SystemScheduler& scheduler,
	SystemContext& systemContext,
	EditorManager& editorManager,
	RuntimeWorldBaker& runtimeWorldBaker,
	ManagedScriptBuildService& scriptBuildService,
	bool& requestFrameDeltaReset,
	std::function<bool()> isPrefabEditing,
	std::function<bool()> saveAllEditScenes,
	std::function<void()> refreshActiveWorldContext) :
	assetDatabase_(assetDatabase),
	worldManager_(worldManager),
	editScenes_(editScenes),
	playScenes_(playScenes),
	sceneSystem_(sceneSystem),
	scheduler_(scheduler),
	systemContext_(systemContext),
	editorManager_(editorManager),
	runtimeWorldBaker_(runtimeWorldBaker),
	scriptBuildService_(scriptBuildService),
	requestFrameDeltaReset_(requestFrameDeltaReset),
	isPrefabEditing_(isPrefabEditing),
	saveAllEditScenes_(saveAllEditScenes),
	refreshActiveWorldContext_(refreshActiveWorldContext) {
}

void Engine::EditorPlaySession::HandlePlayToggle() {

	if constexpr (!BuildConfig::kEditorEnabled) {
		return;
	}

	if (pendingPlayStart_) {

		(void)Input::GetInstance()->TriggerKey(DIK_F5);
		if constexpr (BuildConfig::kEditorEnabled) {
			(void)editorManager_.ConsumePlayToggleRequest();
		}
		ProcessPendingPlayStart();
		return;
	}

	const bool requestedByKeyboard = Input::GetInstance()->TriggerKey(DIK_F5);
	bool requestedByEditor = false;
	if constexpr (BuildConfig::kEditorEnabled) {
		requestedByEditor = editorManager_.ConsumePlayToggleRequest();
	}
	if (!requestedByKeyboard && !requestedByEditor) {
		return;
	}
	requestFrameDeltaReset_ = true;

	if (!worldManager_.IsPlaying()) {

		scriptBuildService_.RequestPlayBuild();
		pendingPlayStart_ = true;
		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Play開始に向けてGameScriptsをビルドまたは再読み込みします");
		ProcessPendingPlayStart();
	} else {

		StopPlayWorld();
	}
}

void Engine::EditorPlaySession::StopPlayWorld() {

	scheduler_.DetachCurrentWorld(systemContext_);
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	runtimeWorldBaker_.Detach();
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(*playWorld));
	}
	worldManager_.DestroyPlayWorld();
	playScenes_ = SceneInstanceManager{};
	playPaused_ = false;
	playFrameStepRequested_ = false;
	(void)ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest();
	refreshActiveWorldContext_();
}

bool Engine::EditorPlaySession::HandleApplicationQuitRequest() {

	if (!ManagedScriptRuntime::GetInstance().ConsumeApplicationQuitRequest()) {
		return false;
	}

	if constexpr (BuildConfig::kEditorEnabled) {

		if (!worldManager_.IsPlaying()) {
			return false;
		}
		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Application.Quit要求を受けたためEditへ戻ります");
		requestFrameDeltaReset_ = true;
		StopPlayWorld();
		return true;
	} else {

		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: Application.Quit要求を受けたため終了します");
		WinApp::RequestCloseWindow();
		return false;
	}
}

void Engine::EditorPlaySession::ProcessPendingPlayStart() {

	const ManagedScriptBuildService::PlayBuildResult result = scriptBuildService_.PollPlayBuild();
	if (result == ManagedScriptBuildService::PlayBuildResult::Pending) {
		return;
	}

	pendingPlayStart_ = false;
	if (result == ManagedScriptBuildService::PlayBuildResult::Failed) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: GameScriptsのビルドまたは再読み込みに失敗したためPlayを中止します");
		return;
	}
	if (!isPrefabEditing_() && !saveAllEditScenes_()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: シーン保存に失敗したためPlayを中止します");
		return;
	}

	StartPlayWorld();
}

void Engine::EditorPlaySession::StartPlayWorld() {

	ScriptProfiler::GetInstance().Configure(ScriptProfiler::GetInstance().IsEnabled(), {}, 0);

	auto& scriptRuntime = ManagedScriptRuntime::GetInstance();

	bool waitForManagedDebuggerOnPlay = false;
	if constexpr (BuildConfig::kEditorEnabled) {
		waitForManagedDebuggerOnPlay = editorManager_.GetLayoutState().waitForManagedDebuggerOnPlay;
	}
	if (waitForManagedDebuggerOnPlay && !scriptRuntime.ActiveAssemblyPath().empty()) {
		scriptRuntime.LoadGameAssemblyFromPath(scriptRuntime.ActiveAssemblyPath(), true);
	}

	nlohmann::json snapshot = editScenes_.SerializeSnapshot(sceneSystem_, worldManager_.GetEditWorld());

	worldManager_.CreatePlayWorld();
	if (!worldManager_.GetPlayWorld() ||
		!playScenes_.LoadSnapshot(assetDatabase_, sceneSystem_, *worldManager_.GetPlayWorld(), snapshot)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: シーンSnapshotを読み込めないためPlayを開始できません");

		playScenes_ = SceneInstanceManager{};
		worldManager_.DestroyPlayWorld();
		playPaused_ = false;
		playFrameStepRequested_ = false;
		return;
	}
	// Play最初のLifecycleより先にRuntime派生データを完成させる
	runtimeWorldBaker_.Attach(*worldManager_.GetPlayWorld(), &assetDatabase_);
	runtimeWorldBaker_.BakeAll();
	ManagedWorldRegistry::GetInstance().Register(*worldManager_.GetPlayWorld());
	ManagedScriptRuntime::BeginPlayTime(worldManager_.GetPlayWorld());
	systemContext_.time = 0.0f;
	systemContext_.unscaledTime = 0.0f;
	systemContext_.smoothDeltaTime = 0.0f;
	playPaused_ = false;
	playFrameStepRequested_ = false;
	requestFrameDeltaReset_ = true;
	playWorldJustStarted_ = true;
}

void Engine::EditorPlaySession::HandlePlayPauseRequests() {

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

bool Engine::EditorPlaySession::ShouldAdvanceActiveWorld() const {

	return !worldManager_.IsPlaying() || !playPaused_ || playFrameStepRequested_;
}

bool Engine::EditorPlaySession::ConsumeJustStarted() {

	const bool started = playWorldJustStarted_;
	playWorldJustStarted_ = false;
	return started;
}
