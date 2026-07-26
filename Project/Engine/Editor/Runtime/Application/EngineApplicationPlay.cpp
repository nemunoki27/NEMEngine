#include "EngineApplication.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>

//============================================================================
//	EngineApplication play methods
//============================================================================
void Engine::EngineApplication::HandlePlayToggle() {

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
			"EngineApplication: Play requested. preparing GameScripts (build/reload)...");
		ProcessPendingPlayStart();
	} else {

		StopPlayWorld();
	}
}

void Engine::EngineApplication::StopPlayWorld() {

	scheduler_.DetachCurrentWorld(systemContext_);
	if (ECSWorld* playWorld = worldManager_.GetPlayWorld()) {
		ManagedWorldRegistry::GetInstance().Unregister(
			ManagedWorldRegistry::GetInstance().TryGetHandle(*playWorld));
	}
	worldManager_.DestroyPlayWorld();
	playScenes_ = SceneInstanceManager{};
	playPaused_ = false;
	playFrameStepRequested_ = false;
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
	CollisionSettings::GetInstance().BindGlobal();

	if constexpr (BuildConfig::kEditorEnabled) {

		editorContext_.isPlaying = worldManager_.IsPlaying();
		editorContext_.isPlayPaused = playPaused_;
		editorContext_.activeScenePath = activeScenePath_;
		editorContext_.activeSceneHeader = header;
		editorContext_.activeSceneAsset = activeSceneInstance ? activeSceneInstance->sceneAsset : activeScene_;
		editorContext_.activeSceneInstanceID = activeSceneInstance ? activeSceneInstance->instanceID : UUID{};
		editorContext_.activeSceneDirty =
			editorManager_.IsSceneDirty(editorContext_.activeSceneAsset);
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
			(!prefabStages_.empty() && !prefabStages_.back().inContext) ?
			&prefabStages_.back().environmentEntities : nullptr;
	}
}

void Engine::EngineApplication::ProcessPendingPlayStart() {

	const ManagedScriptBuildService::PlayBuildResult result = scriptBuildService_.PollPlayBuild();
	if (result == ManagedScriptBuildService::PlayBuildResult::Pending) {
		return;
	}

	pendingPlayStart_ = false;
	if (result == ManagedScriptBuildService::PlayBuildResult::Failed) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: Play canceled. GameScripts build/reload failed. Staying in Edit mode.");
		return;
	}
	if (!IsPrefabEditing() && !SaveAllEditScenes()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: Play canceled. Scene save failed. Staying in Edit mode.");
		return;
	}

	StartPlayWorld();
}

void Engine::EngineApplication::StartPlayWorld() {

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
		!playScenes_.LoadSnapshot(assetDataBase_, sceneSystem_, *worldManager_.GetPlayWorld(), snapshot)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: failed to enter Play mode. Scene snapshot load failed.");

		playScenes_ = SceneInstanceManager{};
		worldManager_.DestroyPlayWorld();
		playPaused_ = false;
		playFrameStepRequested_ = false;
		return;
	}
	ManagedWorldRegistry::GetInstance().Register(*worldManager_.GetPlayWorld());
	ManagedScriptRuntime::BeginPlayTime(worldManager_.GetPlayWorld());
	playPaused_ = false;
	playFrameStepRequested_ = false;
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
