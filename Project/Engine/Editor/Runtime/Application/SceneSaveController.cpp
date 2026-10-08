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
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <exception>
#include <algorithm>
#include <utility>
#include <vector>

using namespace Engine;

Engine::SceneSaveController::SceneSaveController(AssetDatabase& assetDatabase,
	WorldManager& worldManager,
	SceneInstanceManager& editScenes,
	SceneSystem& sceneSystem,
	EditorManager& editorManager,
	std::function<void()> restoreEditModeUIVisuals) :
	assetDatabase_(assetDatabase),
	worldManager_(worldManager),
	editScenes_(editScenes),
	sceneSystem_(sceneSystem),
	editorManager_(editorManager),
	restoreEditModeUIVisuals_(restoreEditModeUIVisuals) {
}

bool Engine::SceneSaveController::SaveActiveEditScene() {

	const SceneInstance* activeScene = editScenes_.GetActive();
	return activeScene && SaveEditScene(activeScene->instanceID);
}

bool Engine::SceneSaveController::SaveEditScene(UUID instanceID) {

	const SceneInstance* scene = editScenes_.Find(instanceID);
	if (!scene || !scene->sceneAsset || scene->persistent) {
		return false;
	}
	const AssetID sceneAsset = scene->sceneAsset;
	if (sceneSaveJob_) {

		// 同じSceneの連打はまとめ、別Sceneの要求も保持する
		if (std::find(queuedInstances_.begin(), queuedInstances_.end(), instanceID) == queuedInstances_.end()) {
			queuedInstances_.push_back(instanceID);
		}
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
		"EngineApplication: シーン保存用Snapshotを複製しました Asset={} 経過={}ms",
		ToString(sceneAsset), captureElapsed);

	SceneSaveJob job{};
	job.sceneAsset = sceneAsset;
	for (UUID id : editScenes_.FindInstanceIDs(sceneAsset)) {
		job.dirtyRevisions[id] = editorManager_.GetSceneDirtyRevision(sceneAsset, id);
	}
	job.scenePath = Algorithm::PathToUTF8(assetDatabase_.ResolveFullPath(sceneAsset));
	job.startedAt = captureStartedAt;
	job.result = std::async(std::launch::async,
		[worldSnapshot = std::move(worldSnapshot),
		scenesSnapshot = std::move(scenesSnapshot),
		databaseSnapshot = std::move(databaseSnapshot),
		sceneAsset, instanceID, storage = sceneSystem_.GetStorage()]() mutable {

			SceneSystem sceneSystem{ std::move(storage) };
			SceneSaveResult result;
			SceneSaveSnapshot snapshot{};
			if (!scenesSnapshot.CaptureSave(
				databaseSnapshot, sceneSystem,
				*worldSnapshot, sceneAsset, snapshot, instanceID)) {
				return result;
			}
			// 保存内容と一致するInstanceだけを完了時に解除する
			for (UUID id : scenesSnapshot.FindInstanceIDs(sceneAsset)) {
				SceneSaveSnapshot other;
				if (id == instanceID ||
					(scenesSnapshot.CaptureSave(databaseSnapshot, sceneSystem, *worldSnapshot, sceneAsset, other, id) &&
						other.root == snapshot.root && other.useExternalActors == snapshot.useExternalActors)) {
					result.matchingInstances.push_back(id);
				}
			}
			result.succeeded = SceneSystem::WriteSaveSnapshot(std::move(snapshot));
			return result;
		});
	sceneSaveJob_.emplace(std::move(job));
	return true;
}

std::vector<Engine::SceneSaveConflictChoice>
Engine::SceneSaveController::ConsumePendingConflicts() {

	std::vector<SceneSaveConflictChoice> conflicts = std::move(pendingConflicts_);
	pendingConflicts_.clear();
	return conflicts;
}

Engine::SceneSaveOutcome Engine::SceneSaveController::SaveAllEditScenes(
	const std::unordered_map<AssetID, UUID>& selectedInstances) {

	if (!WaitForSceneSave()) {
		return SceneSaveOutcome::Failed;
	}
	restoreEditModeUIVisuals_();

	// 同じ保存先の競合は最初のファイルを書き込む前に検出する
	std::vector<SceneSaveSnapshot> snapshots;
	std::vector<AssetID> conflicts;
	if (!editScenes_.CaptureAllSaves(assetDatabase_, sceneSystem_, worldManager_.GetEditWorld(), snapshots, conflicts,
		selectedInstances)) {
		pendingConflicts_.clear();
		for (AssetID asset : conflicts) {
			SceneSaveConflictChoice choice{};
			choice.sceneAsset = asset;
			choice.instanceIDs = editScenes_.FindInstanceIDs(asset);
			pendingConflicts_.emplace_back(std::move(choice));
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: 同じSceneの編集内容が異なります。保存元Instanceを選択してください Asset={}", ToString(asset));
		}
		return pendingConflicts_.empty() ? SceneSaveOutcome::Failed : SceneSaveOutcome::Conflict;
	}
	pendingConflicts_.clear();
	for (auto& snapshot : snapshots) {
		const AssetID sceneAsset = snapshot.sceneAsset;
		if (!SceneSystem::WriteSaveSnapshot(std::move(snapshot))) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"EngineApplication: シーン保存に失敗しました Asset={}", ToString(sceneAsset));
			return SceneSaveOutcome::Failed;
		}
		// 保存元を選んだAssetには別Instanceの未保存編集が残る
		if (!selectedInstances.contains(sceneAsset)) {
			editorManager_.MarkSceneSaved(sceneAsset);
		} else {
			const UUID selected = selectedInstances.at(sceneAsset);
			editorManager_.MarkSceneInstanceSaved(sceneAsset, selected);
			for (UUID instance : editScenes_.FindInstanceIDs(sceneAsset)) {
				if (instance != selected) {
					editorManager_.MarkSceneInstanceDirty(sceneAsset, instance);
				}
			}
		}
	}

	assetDatabase_.RebuildMeta();
	Logger::Output(LogType::Engine, spdlog::level::info,
		"EngineApplication: 読み込み済みシーンを保存しました 数={}", snapshots.size());
	return editorManager_.HasDirtyScenes() ? SceneSaveOutcome::UnsavedInstances : SceneSaveOutcome::Saved;
}

bool Engine::SceneSaveController::FinishSceneSave(
	bool wait, bool* outSucceeded) {

	if (outSucceeded) {
		*outSucceeded = true;
	}
	if (!sceneSaveJob_) {
		return true;
	}

	std::future<SceneSaveResult>& result = sceneSaveJob_->result;
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
	const auto dirtyRevisions = sceneSaveJob_->dirtyRevisions;
	const std::string scenePath =
		sceneSaveJob_->scenePath;
	const auto elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() -
			sceneSaveJob_->startedAt).count();
	bool succeeded = false;
	SceneSaveResult saved;
	try {
		saved = result.get();
		succeeded = saved.succeeded;
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
		for (const auto& [instanceID, revision] : dirtyRevisions) {
			if (!editScenes_.Find(instanceID)) {
				continue;
			}
			if (std::find(saved.matchingInstances.begin(), saved.matchingInstances.end(), instanceID) != saved.matchingInstances.end()) {
				editorManager_.MarkSceneSaved(sceneAsset, revision, instanceID);
			} else {
				// ファイルと異なる別Instanceは未保存として残す
				editorManager_.MarkSceneInstanceDirty(sceneAsset, instanceID);
			}
		}
		// 保存中に追加されたInstanceは内容一致を確認できていない
		for (UUID instanceID : editScenes_.FindInstanceIDs(sceneAsset)) {
			if (!dirtyRevisions.contains(instanceID)) {
				editorManager_.MarkSceneInstanceDirty(sceneAsset, instanceID);
			}
		}
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
	if (queuedInstances_.empty()) {
		return;
	}

	const UUID requested = queuedInstances_.front();
	queuedInstances_.pop_front();
	SaveEditScene(requested);
}

bool Engine::SceneSaveController::WaitForSceneSave() {

	bool allSucceeded = true;
	while (sceneSaveJob_ || !queuedInstances_.empty()) {

		bool succeeded = true;
		FinishSceneSave(true, &succeeded);
		allSucceeded = allSucceeded && succeeded;
		if (!queuedInstances_.empty()) {

			const UUID requested = queuedInstances_.front();
			queuedInstances_.pop_front();
			allSucceeded = SaveEditScene(requested) && allSucceeded;
		}
	}
	return allSucceeded;
}
