#include "EditorEntityDuplicateUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotDuplicator.h>

//============================================================================
//	EditorEntityDuplicateUtility classMethods
//============================================================================
namespace {

	// base_N の形式ならベース名を返し、そうでなければそのまま返す
	std::string NormalizeDuplicateBaseName(const std::string_view& sourceName) {

		std::string name = sourceName.empty() ? "Entity" : std::string(sourceName);

		std::string base;
		uint32_t index = 0;
		if (Engine::SceneAuthoring::TryParseIndexedName(name, base, index)) {
			return base.empty() ? name : base;
		}
		return name;
	}
	// エンティティとその子孫にシーンインスタンスIDとソースアセットを設定する
	void PropagateSceneRuntimeState(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::UUID sceneInstanceID, Engine::AssetID sourceAsset) {

		for (const Engine::Entity& current : Engine::HierarchyUtility::CollectLogicalSubtree(world, entity)) {

			if (!world.HasComponent<Engine::SceneObjectComponent>(current)) {
				continue;
			}
			auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(current);
			if (sceneInstanceID) {
				sceneObject.sceneInstanceID = sceneInstanceID;
			}
			if (!sceneObject.sourceAsset && sourceAsset) {
				sceneObject.sourceAsset = sourceAsset;
			}
		}
	}
}

std::string Engine::EditorEntityDuplicateUtility::MakeUniqueDuplicatedName(
	ECSWorld& world, const std::string_view& sourceName) {

	// 複製元は必ず存在するので、ベース名へ正規化して共通の一意名生成へ委ねれば base_N になる
	const std::string baseName = NormalizeDuplicateBaseName(sourceName);
	return SceneAuthoring::MakeUniqueEntityName(world, baseName);
}

void Engine::EditorEntityDuplicateUtility::ClearRootParentLink(EditorEntityTreeSnapshot& snapshot) {

	EntitySnapshotDuplicator::ClearRootParentLink(snapshot);
}

Engine::Entity Engine::EditorEntityDuplicateUtility::InstantiatePreparedSnapshot(ECSWorld& world,
	const EditorEntityTreeSnapshot& preparedSnapshot, UUID externalParentStableUUID) {

	// スナップショットが空なら何もしない
	if (preparedSnapshot.IsEmpty()) {
		return Entity::Null();
	}

	// スナップショットからエンティティを生成する
	std::vector<Entity> createdEntities = EditorEntitySnapshotUtility::RestoreSubtree(world, preparedSnapshot);

	// 複製サブツリー内部だけ親子リンクを組み立てる
	HierarchySystem hierarchySystem;
	hierarchySystem.RebuildRuntimeLinks(world, createdEntities);
	// ルートエンティティを取得する
	Entity root = world.FindByUUID(preparedSnapshot.rootStableUUID);
	if (!world.IsAlive(root)) {
		return Entity::Null();
	}

	// ランタイム情報をルート以下に伝播する
	UUID resolvedSceneInstanceID = preparedSnapshot.ownerSceneInstanceID;
	AssetID resolvedSourceAsset = preparedSnapshot.ownerSourceAsset;

	// 外部親があるならルートをその子にする
	if (externalParentStableUUID) {

		// 外部親のEntityをUUIDから検索し見つかって生きているならルートの親にする
		Entity parent = world.FindByUUID(externalParentStableUUID);
		if (world.IsAlive(parent)) {

			hierarchySystem.SetParent(world, root, parent);
			if (world.HasComponent<SceneObjectComponent>(parent)) {

				const auto& parentSceneObject = world.GetComponent<SceneObjectComponent>(parent);
				if (parentSceneObject.sceneInstanceID) {
					resolvedSceneInstanceID = parentSceneObject.sceneInstanceID;
				}
				if (parentSceneObject.sourceAsset) {
					resolvedSourceAsset = parentSceneObject.sourceAsset;
				}
			}
		}
	}
	// シーンインスタンスIDかソースアセットのどちらかがあれば、ルート以下に伝播する
	if (resolvedSceneInstanceID || resolvedSourceAsset) {

		PropagateSceneRuntimeState(world, root, resolvedSceneInstanceID, resolvedSourceAsset);
	}

	return root;
}
