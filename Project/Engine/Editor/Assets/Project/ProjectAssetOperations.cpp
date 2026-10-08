#include "ProjectAssetOperations.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileUtility.h"
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDocumentPublication.h>
#include <Engine/Core/Assets/Database/AssetDocumentRecovery.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabSnapshotBuilder.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>

bool Engine::ProjectAssetOperations::SavePrefab(AssetDatabase& database, ECSWorld& world, const Entity& entity,
	ProjectAssetSource source, const std::string& directoryVirtualPath, ProjectAssetFileResult& result) {

	result = {};
	const auto lifetime = world.GetLifetime();
	if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) {
		result.message = "Prefabの保存対象が無効です";
		return false;
	}
	bool published = false;
	try {
		// 名前と公開先を決め、空のPrefabは作らない
		std::string prefabName = "NewPrefab";
		if (const auto* name = world.TryGetComponent<NameComponent>(entity); name && !name->name.empty()) {
			prefabName = name->name;
		}
		result = ProjectAssetFileUtility::PlanCreate(source, directoryVirtualPath, ProjectAssetFileKind::Prefab, prefabName);
		if (!result.success) {
			return false;
		}
		result.success = false;
		AssetDocumentChange change;
		if (!AssetDocumentPublication::Prepare(database, result.assetPath, AssetType::Prefab, change, result.message)) {
			return false;
		}
		if (change.fileRevision != "missing" || change.metaRevision != "missing" || database.FindByPath(result.assetPath)) {
			result.message = "Prefabの作成先が変更されました";
			return false;
		}
		// 同じGUIDで本体とmetaを準備する
		const auto entities = HierarchyUtility::CollectLogicalSubtree(world, entity);
		const UUID prefabInstanceID = UUID::New();
		if (!PrefabSnapshotBuilder::BuildDocument(
				database, world, entity, entities, result.assetPath, prefabInstanceID, change.metadata.guid, change.document)) {
			result.message = "Prefab文書を構築できません";
			return false;
		}
		if (!world.IsAlive(entity) || world.IsPendingDestroy(entity)) {
			result.message = "Prefabの保存中に対象が変更されました";
			return false;
		}
		// 保存失敗では文書とmetaと索引を元へ戻す
		change.canonicalize = true;
		if (!AssetDocumentPublication::Commit(database, std::span<const AssetDocumentChange>(&change, 1),
				AssetDocumentRecovery::MakeScope(AssetDocumentSaveKind::Prefab), result.message)) {
			return false;
		}
		published = true;
		PrefabSystem prefabSystem;
		prefabSystem.SetPrefabLinkToSubtree(world, entity, change.metadata.guid, prefabInstanceID);
		result.success = true;
		return true;
	} catch (const std::exception& error) {
		// 終了したWorldへUI処理を戻さない
		if (!lifetime->IsAlive()) {
			throw;
		}
		result.message = published ? "Prefabは保存しましたがSceneへ接続できません: " + std::string(error.what()) : error.what();
		Logger::Output(
			LogType::Engine, spdlog::level::warn, "ProjectPanel: Prefabの保存に失敗しました 内容={}", result.message);
		return false;
	}
}

void Engine::ProjectAssetOperations::UpdateLoadedSceneName(
	const EditorPanelContext& context, AssetID assetID, const std::filesystem::path& path) {

	if (!context.editorContext || !context.editorContext->sceneInstances) {
		return;
	}

	const std::string sceneName = MakeSceneAssetName(path);
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
