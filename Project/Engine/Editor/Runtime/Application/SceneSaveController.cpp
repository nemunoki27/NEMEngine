#include "SceneSaveController.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/WorldManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Editor/Core/EditorManager.h>
#include <Engine/Core/Foundation/Build/BuildConfig.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>
#include <unordered_set>

using namespace Engine;

Engine::SceneSaveController::SceneSaveController(AssetDatabase& assetDatabase,
	WorldManager& worldManager,
	SceneInstanceManager& editScenes,
	SceneSystem& sceneSystem,
	EditorManager& editorManager,
	const std::string& activeScenePath,
	std::function<void()> restoreEditModeUIVisuals) :
	assetDatabase_(assetDatabase),
	worldManager_(worldManager),
	editScenes_(editScenes),
	sceneSystem_(sceneSystem),
	editorManager_(editorManager),
	activeScenePath_(activeScenePath),
	restoreEditModeUIVisuals_(restoreEditModeUIVisuals) {
}

bool Engine::SceneSaveController::SaveActiveEditScene() {

	const SceneInstance* activeScene = editScenes_.GetActive();
	const AssetID sceneAsset = activeScene ? activeScene->sceneAsset : AssetID{};
	if (!activeScene || !sceneAsset) {
		return false;
	}
	if (sceneSaveJob_) {

		// 保存中の再要求は完了直後に最新ワールドをもう一度取得する
		sceneSaveQueued_ = true;
		return true;
	}
	restoreEditModeUIVisuals_();

	const auto captureStartedAt =
		std::chrono::steady_clock::now();
	std::unique_ptr<ECSWorld> worldSnapshot =
		worldManager_.GetEditWorld().
		CloneForSerialization();
	SceneInstanceManager scenesSnapshot =
		editScenes_;
	AssetDatabase databaseSnapshot =
		assetDatabase_;

	const auto captureElapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() -
			captureStartedAt).count();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: シーン保存用Snapshotを複製しました path={} 経過={}ms",
		activeScenePath_, captureElapsed);

	SceneSaveJob job{};
	job.sceneAsset = sceneAsset;
	job.dirtyRevision =
		editorManager_.GetSceneDirtyRevision(sceneAsset);
	job.scenePath = activeScenePath_;
	job.startedAt = captureStartedAt;
	job.result = std::async(std::launch::async,
		[worldSnapshot = std::move(worldSnapshot),
		scenesSnapshot = std::move(scenesSnapshot),
		databaseSnapshot = std::move(databaseSnapshot),
		sceneAsset, storage = sceneSystem_.GetStorage()]() mutable {

			SceneSystem sceneSystem{ std::move(storage) };
			SceneSaveSnapshot snapshot{};
			if (!scenesSnapshot.CaptureSave(
				databaseSnapshot, sceneSystem,
				*worldSnapshot, sceneAsset, snapshot)) {
				return false;
			}
			return SceneSystem::WriteSaveSnapshot(
				std::move(snapshot));
		});
	sceneSaveJob_.emplace(std::move(job));
	return true;
}

bool Engine::SceneSaveController::SaveAllEditScenes() {

	if (!WaitForSceneSave()) {
		return false;
	}
	restoreEditModeUIVisuals_();

	std::unordered_set<AssetID> savedAssets;
	for (const SceneInstance& scene : editScenes_.GetAll()) {

		if (!scene.sceneAsset || !savedAssets.insert(scene.sceneAsset).second) {
			continue;
		}
		if (!editScenes_.Save(
			assetDatabase_, sceneSystem_, worldManager_.GetEditWorld(), scene.sceneAsset)) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: シーン保存に失敗しました Asset={}", ToString(scene.sceneAsset));
			return false;
		}
	}

	assetDatabase_.RebuildMeta();
	if constexpr (BuildConfig::kEditorEnabled) {
		editorManager_.MarkAllScenesSaved();
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: 読み込み済みシーンを保存しました 数={}", savedAssets.size());
	return true;
}

bool Engine::SceneSaveController::FinishSceneSave(
	bool wait, bool* outSucceeded) {

	if (outSucceeded) {
		*outSucceeded = true;
	}
	if (!sceneSaveJob_) {
		return true;
	}

	std::future<bool>& result = sceneSaveJob_->result;
	if (!wait && result.wait_for(
		std::chrono::milliseconds(0)) !=
		std::future_status::ready) {
		return false;
	}
	if (wait) {
		result.wait();
	}

	const AssetID sceneAsset =
		sceneSaveJob_->sceneAsset;
	const uint64_t dirtyRevision =
		sceneSaveJob_->dirtyRevision;
	const std::string scenePath =
		sceneSaveJob_->scenePath;
	const auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() -
			sceneSaveJob_->startedAt).count();
	bool succeeded = false;
	try {
		succeeded = result.get();
	}
	catch (const std::exception& exception) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"EngineApplication: シーン保存Workerが失敗しました path={} 内容={}",
			scenePath, exception.what());
	}
	sceneSaveJob_.reset();

	if (succeeded) {

		// AssetDatabaseとEditor状態はメインスレッドだけで更新する
		assetDatabase_.RebuildMeta();
		editorManager_.MarkSceneSaved(
			sceneAsset, dirtyRevision);
		Logger::Output(LogType::Engine, spdlog::level::info,
			"EngineApplication: アクティブシーンを保存しました path={} 経過={}ms",
			scenePath, elapsed);
	} else {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"EngineApplication: アクティブシーンの保存に失敗しました path={}",
			scenePath);
	}
	if (outSucceeded) {
		*outSucceeded = succeeded;
	}
	return true;
}

void Engine::SceneSaveController::UpdateSceneSave() {

	bool succeeded = true;
	if (!FinishSceneSave(false, &succeeded)) {
		return;
	}
	if (!sceneSaveQueued_) {
		return;
	}

	sceneSaveQueued_ = false;
	SaveActiveEditScene();
}

bool Engine::SceneSaveController::WaitForSceneSave() {

	bool allSucceeded = true;
	while (sceneSaveJob_) {

		bool succeeded = true;
		FinishSceneSave(true, &succeeded);
		allSucceeded = allSucceeded && succeeded;
		if (sceneSaveQueued_) {

			sceneSaveQueued_ = false;
			if (!SaveActiveEditScene()) {
				return false;
			}
		}
	}
	return allSucceeded;
}
