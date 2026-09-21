#include "SceneInstantiator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>

// c++
#include <vector>

using namespace Engine::SceneDocument;

bool Engine::SceneInstantiator::LoadFromJson(const nlohmann::json& sourceRoot, ECSWorld& world,
	AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
	std::vector<Entity>* outCreatedEntities) {

	auto root = sourceRoot;
	std::string recoveryDiagnostic;
	if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(root, sourceAsset, recoveryDiagnostic, assetDatabase)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] Prefab旧データを復旧できません Scene={} 詳細={}", ToString(sourceAsset), recoveryDiagnostic);
		return false;
	}
	// 生成を始める前に全インスタンスを検証する
	if (root.contains("PrefabInstances") && root["PrefabInstances"].is_array()) {
		for (const auto& instance : root["PrefabInstances"]) {
			PrefabInstanceData validated;
			if (!FromJson(instance, validated)) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[SceneSystem] Prefabインスタンスの保存データが不正です Scene={} InstanceID={}",
					ToString(sourceAsset), instance.value("InstanceID", ""));
				return false;
			}
		}
	}
	if (!recoveryDiagnostic.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[SceneSystem] 旧Prefab対応を先頭IDへ統合しました Scene={} 通常保存で確定します 詳細={}",
			ToString(sourceAsset), recoveryDiagnostic);
	}

	// ルートがオブジェクトでなければ失敗
	if (!root.is_object()) {
		return false;
	}
	if (!ValidateSerializedLocalFileIDs(root)) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] シーン内に不正または重複したLocalFileIDがあります");
		return false;
	}

	// fat保存された実体を読み込む、Entitiesが配列でなければ空として扱いPrefabInstancesのみ処理する
	const nlohmann::json emptyArray = nlohmann::json::array();
	const nlohmann::json& entitiesNode =
		(root.contains("Entities") && root["Entities"].is_array()) ? root["Entities"] : emptyArray;

	// "Entities"配列をループしてエンティティを作成し、コンポーネントを追加する
	for (const auto& entityJson : entitiesNode) {

		const UUID localFileID =
			FromString16Hex(entityJson.value("LocalFileID", std::string{}));

		const nlohmann::json* components =
			(entityJson.contains("Components") && entityJson["Components"].is_object()) ?
			&entityJson["Components"] : nullptr;
		std::vector<uint32_t> componentTypeIDs;
		if (components) {
			componentTypeIDs.reserve(components->size());
			for (auto it = components->begin(); it != components->end(); ++it) {

				const ComponentTypeInfo* info =
					ComponentTypeRegistry::GetInstance().FindByName(it.key());
				if (!info) {
					Logger::Output(LogType::Engine, spdlog::level::err,
						"[SceneSystem] 未登録のComponentTypeです: {}", it.key());
					return false;
				}
				componentTypeIDs.emplace_back(info->id);
			}
		}

		// JSONに含まれるコンポーネントを含む最終アーキタイプへ直接作成
		Entity entity = SceneAuthoring::CreateGameObject(world, "Entity", componentTypeIDs);

		// 作成されたエンティティを出力する
		if (outCreatedEntities) {

			outCreatedEntities->emplace_back(entity);
		}

		// "Components"オブジェクトが存在する場合はコンポーネントを追加する
		if (components) {

			for (auto it = components->begin(); it != components->end(); ++it) {

				const std::string& typeName = it.key();
				const nlohmann::json& data = it.value();
				world.ApplyComponentJson(entity, typeName, data);
			}
		}
		// JSONからSceneObjectを読み直した後に、ランタイム所属情報を設定する
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = localFileID;
			sceneObject.sourceAsset = sourceAsset;
			sceneObject.sceneInstanceID = sceneInstanceID;
		}
		// ロード直後に、ワールド実体のサブメッシュを正規化する
		if (assetDatabase && world.HasComponent<MeshRendererComponent>(entity)) {

			MeshSubMeshAuthoring::SyncEntity(assetDatabase, world, entity, true);
		}
	}

	// 薄い差分形式で保存されたプレファブインスタンスを展開する
	if (assetDatabase && root.contains("PrefabInstances") && root["PrefabInstances"].is_array()) {

		HierarchySystem hierarchySystem{};
		for (const auto& instanceJson : root["PrefabInstances"]) {

			PrefabInstanceData data{};
			if (!FromJson(instanceJson, data)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[SceneSystem] Prefabインスタンスの保存データが不正です");
				return false;
			}
			const Entity instanceRoot =
				PrefabOverrideUtility::RebuildInstance(world, *assetDatabase, hierarchySystem, data, sceneInstanceID);
			if (!world.IsAlive(instanceRoot)) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[SceneSystem] Prefabインスタンスを復元できません AssetID={} InstanceID={}",
					ToString(data.prefabAsset), ToString(data.instanceID));
				return false;
			}
			// 生成したインスタンスの実体を作成リストへ加える、追加実体はsceneInstanceIDで保存時に回収される
			if (outCreatedEntities) {
				for (const Entity& entity : PrefabOverrideUtility::CollectInstanceEntities(world, data.instanceID)) {
					outCreatedEntities->emplace_back(entity);
				}
			}
		}
	}

	// 全Prefab生成後に外部親参照を含む階層をまとめて解決する
	std::vector<Entity> hierarchyScope;
	hierarchyScope.reserve(world.GetRecordCount());
	world.ForEachAliveEntity([&](Entity entity) {
		hierarchyScope.emplace_back(entity);
		});
	HierarchySystem hierarchySystem{};
	hierarchySystem.RebuildRuntimeLinks(world, hierarchyScope);
	return true;
}
