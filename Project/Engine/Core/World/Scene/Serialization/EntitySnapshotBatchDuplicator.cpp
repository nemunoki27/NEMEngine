#include "EntitySnapshotBatchDuplicator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotDuplicator.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {

	struct SceneReferenceMap {

		Engine::PrefabReferenceRemapper::LocalFileIDMap localIDs;
		std::unordered_map<Engine::UUID, Engine::AssetID> assets;
	};

	struct DuplicatedIdentity {

		Engine::UUID stableUUID{};
		Engine::UUID localFileID{};
	};

	using IdentityMap = std::unordered_map<Engine::UUID, DuplicatedIdentity>;

	// コピー時に特定できた実体だけを複製先へ張り替える
	void RemapCapturedReference(nlohmann::json& reference, const Engine::SerializedEntitySnapshot& source,
		const IdentityMap& identities) {

		Engine::AssetID asset;
		Engine::UUID localID;
		if (!Engine::PrefabReferenceRemapper::TryReadEntityReference(reference, asset, localID)) {
			return;
		}
		const auto captured = std::find_if(source.referenceTargets.begin(), source.referenceTargets.end(),
			[&](const auto& target) { return target.sourceAsset == asset && target.localFileID == localID; });
		if (captured == source.referenceTargets.end()) {
			return;
		}
		const auto replacement = identities.find(captured->targetStableUUID);
		if (replacement != identities.end() && replacement->second.localFileID) {
			reference["localFileId"] = Engine::ToString(replacement->second.localFileID);
		}
	}

	// 保存値から所属内の識別子を読む
	Engine::UUID ReadLocalID(const Engine::SerializedEntitySnapshot& entity) {

		const auto membership = entity.components.find("SceneObject");
		return membership == entity.components.end() ? Engine::UUID{} :
			Engine::FromString16Hex(membership->value("localFileId", std::string{}));
	}
}

//============================================================================
//	EntitySnapshotBatchDuplicator classMethods
//============================================================================
std::vector<Engine::EntityTreeSnapshot> Engine::EntitySnapshotBatchDuplicator::Build(
	std::span<const EntityTreeSnapshot> sources) {

	std::vector<EntityTreeSnapshot> result;
	result.reserve(sources.size());
	std::unordered_set<UUID> sourceIDs;
	IdentityMap identities;
	std::unordered_map<UUID, SceneReferenceMap> scenes;
	for (const auto& source : sources) {
		if (source.IsEmpty()) {
			throw std::invalid_argument("空の階層は一括複製できません");
		}
		EntityTreeSnapshot duplicate;
		EntitySnapshotDuplicator::Build(source, "", duplicate);
		auto& mapping = scenes[source.ownerSceneInstanceID];
		for (size_t index = 0; index < source.entities.size(); ++index) {
			const auto& original = source.entities[index];
			if (!sourceIDs.insert(original.stableUUID).second) {
				throw std::invalid_argument("複製範囲に同じEntityが含まれています");
			}
			const UUID oldID = ReadLocalID(original);
			identities.emplace(original.stableUUID,
				DuplicatedIdentity{ duplicate.entities[index].stableUUID, ReadLocalID(duplicate.entities[index]) });
			if (oldID) {
				if (!mapping.localIDs.emplace(oldID, ReadLocalID(duplicate.entities[index])).second) {
					throw std::invalid_argument("同じSceneのLocalFileIDが重複しています");
				}
				mapping.assets[oldID] = original.sourceAsset ? original.sourceAsset : source.ownerSourceAsset;
			}
		}
		result.emplace_back(std::move(duplicate));
	}

	// 元の値から変換し、同じ参照を二度変換しない
	for (size_t tree = 0; tree < sources.size(); ++tree) {
		const auto& mapping = scenes.at(sources[tree].ownerSceneInstanceID);
		for (size_t index = 0; index < sources[tree].entities.size(); ++index) {
			const auto& original = sources[tree].entities[index];
			auto& duplicate = result[tree].entities[index];
			for (const auto& [type, component] : original.components.items()) {
				// 所属とPrefabの所有関係は階層ごとの変換結果を使う
				if (type == "SceneObject" || type == "PrefabLink" || type == "Hierarchy") {
					continue;
				}
				nlohmann::json converted = component;
				PrefabReferenceRemapper::RemapComponentReferences(type, converted, mapping.localIDs,
					[&](nlohmann::json& reference) {
						if (original.referencesCaptured) {
							RemapCapturedReference(reference, original, identities);
						} else {
							PrefabReferenceRemapper::RemapMatchedEntityReference(reference, mapping.localIDs, mapping.assets);
						}
					});
				duplicate.components[type] = std::move(converted);
			}
			// 次の複製でも元の実体へ戻らないよう対応表も更新
			for (auto& reference : duplicate.referenceTargets) {
				const auto replacement = identities.find(reference.targetStableUUID);
				if (replacement != identities.end() && replacement->second.localFileID) {
					reference.localFileID = replacement->second.localFileID;
					reference.targetStableUUID = replacement->second.stableUUID;
				}
			}
		}
	}
	return result;
}
