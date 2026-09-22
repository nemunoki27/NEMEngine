#include "ProjectAssetOperations.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileUtility.h"
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

bool Engine::ProjectAssetOperations::SavePrefab(AssetDatabase& database, ECSWorld& world, const Entity& entity,
	ProjectAssetSource source, const std::string& directoryVirtualPath, ProjectAssetFileResult& result) {

	// Prefab名はEntity名を優先し、名前がなければNewPrefabにする
	std::string prefabName = "NewPrefab";
	if (world.HasComponent<NameComponent>(entity)) {

		const std::string& entityName = world.GetComponent<NameComponent>(entity).name;
		if (!entityName.empty()) {
			prefabName = entityName;
		}
	}

	result = ProjectAssetFileUtility::Create(
		source,
		directoryVirtualPath,
		ProjectAssetFileKind::Prefab,
		prefabName);
	if (!result.success) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: Prefab Assetの作成に失敗しました 内容={}", result.message);
		return false;
	}

	PrefabSystem prefabSystem{};
	const UUID prefabInstanceID = UUID::New();
	if (!prefabSystem.SavePrefab(database, world, entity, result.assetPath, prefabInstanceID)) {

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ProjectPanel: Prefabの保存に失敗しました path={}", result.assetPath);
		return false;
	}

	const AssetID prefabAsset = database.ImportOrGet(result.assetPath, AssetType::Prefab);
	prefabSystem.SetPrefabLinkToSubtree(world, entity, prefabAsset, prefabInstanceID);

	return true;
}

void Engine::ProjectAssetOperations::UpdateLoadedSceneName(const EditorPanelContext& context,
	AssetID assetID, const std::filesystem::path& path) {

	if (!context.editorContext || !context.editorContext->sceneInstances) {
		return;
	}

	const std::string sceneName =
		MakeSceneAssetName(path);
	SceneInstanceManager& scenes = *context.editorContext->sceneInstances;
	for (const SceneInstance& scene : scenes.GetAll()) {
		if (scene.sceneAsset != assetID) {
			continue;
		}
		if (SceneInstance* loadedScene = scenes.Find(scene.instanceID)) {
			loadedScene->header.name = sceneName;
		}
	}

}
