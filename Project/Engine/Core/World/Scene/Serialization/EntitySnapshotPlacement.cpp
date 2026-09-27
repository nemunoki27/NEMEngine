#include "EntitySnapshotPlacement.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

// c++
#include <stdexcept>
#include <unordered_map>

namespace {

	struct AssetReplacement {

		Engine::AssetID previous{};
		Engine::AssetID current{};
	};
}

//============================================================================
//	EntitySnapshotPlacement classMethods
//============================================================================
void Engine::EntitySnapshotPlacement::Apply(std::span<EntityTreeSnapshot> snapshots,
	std::span<const EntitySnapshotScene> destinations) {

	if (snapshots.size() != destinations.size()) {
		throw std::invalid_argument("生成する階層と配置先Sceneの数が一致しません");
	}
	std::unordered_map<UUID, AssetReplacement> assetsByLocalID;
	std::unordered_map<UUID, AssetID> assetsByStableID;
	for (size_t index = 0; index < snapshots.size(); ++index) {
		auto& snapshot = snapshots[index];
		const auto& destination = destinations[index];
		snapshot.ownerSceneInstanceID = destination.instanceID;
		for (auto& entity : snapshot.entities) {
			const AssetID previous = entity.sourceAsset ? entity.sourceAsset : snapshot.ownerSourceAsset;
			entity.sceneInstanceID = destination.instanceID;
			// Prefab内部は元Assetを保ち、通常Entityだけを配置先へ所属させる
			if (!entity.components.contains("PrefabLink")) {
				entity.sourceAsset = destination.asset;
			} else {
				entity.sourceAsset = previous;
			}
			const auto membership = entity.components.find("SceneObject");
			if (membership != entity.components.end()) {
				const UUID localID = FromString16Hex(membership->value("localFileId", std::string{}));
				if (localID && !assetsByLocalID.emplace(localID, AssetReplacement{ previous, entity.sourceAsset }).second) {
					throw std::invalid_argument("配置対象のLocalFileIDが重複しています");
				}
			}
			assetsByStableID.emplace(entity.stableUUID, entity.sourceAsset);
		}
		const auto root = assetsByStableID.find(snapshot.rootStableUUID);
		if (root == assetsByStableID.end()) {
			throw std::invalid_argument("配置対象のルートがありません");
		}
		snapshot.ownerSourceAsset = root->second;
	}

	// 全配置先を確定してから内部参照のAssetを更新する
	const PrefabReferenceRemapper::LocalFileIDMap unchanged;
	for (auto& snapshot : snapshots) {
		for (auto& entity : snapshot.entities) {
			for (auto& [type, component] : entity.components.items()) {
				PrefabReferenceRemapper::RemapComponentReferences(type, component, unchanged, [&](nlohmann::json& reference) {
					AssetID asset;
					UUID localID;
					if (!PrefabReferenceRemapper::TryReadEntityReference(reference, asset, localID)) {
						return;
					}
					const auto replacement = assetsByLocalID.find(localID);
					if (replacement == assetsByLocalID.end() || (asset && asset != replacement->second.previous)) {
						return;
					}
					reference["sourceAsset"] = replacement->second.current ? ToString(replacement->second.current) : "";
				});
			}
			for (auto& reference : entity.referenceTargets) {
				const auto replacement = assetsByStableID.find(reference.targetStableUUID);
				if (replacement != assetsByStableID.end()) {
					reference.sourceAsset = replacement->second;
				}
			}
		}
	}
}
