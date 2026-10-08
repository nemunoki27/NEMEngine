#include "PrefabNestedInstanceBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceRebuilder.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <unordered_set>

//============================================================================
//	PrefabNestedInstanceBuilder classMethods
//============================================================================
Engine::PrefabNestedInstanceBuilder::PrefabNestedInstanceBuilder(PrefabGenerationContext& context,
	const PrefabInstantiateDesc& desc, PrefabInstantiateResult& result,
	PrefabReferenceRemapper::LocalFileIDMap& localMap) :
	context_(context), desc_(desc), result_(result), localMap_(localMap) {}

const Engine::PrefabInstanceData* Engine::PrefabNestedInstanceBuilder::FindRestored(UUID nestedSlotID) const {

	if (!desc_.nestedInstanceRemap) {
		return nullptr;
	}
	for (const auto& nested : *desc_.nestedInstanceRemap) {
		if (nested.nestedSlotID == nestedSlotID) {
			return &nested;
		}
	}
	return nullptr;
}

void Engine::PrefabNestedInstanceBuilder::FreshenData(PrefabInstanceData& data, UUID ownerInstanceID,
	const PrefabReferenceRemapper::LocalFileIDMap& externalMap, bool preserveLocalFileIDs) {

	PrefabReferenceRemapper::LocalFileIDMap localMap;
	// Entityの採番を終えてから参照を書き換える
	for (auto& [prefabLocalFileID, sceneLocalFileID] : data.entityMap) {

		const UUID previous = sceneLocalFileID;
		if (!preserveLocalFileIDs) {
			sceneLocalFileID = context_.AllocateLocalFileID();
		}
		localMap.emplace(previous, sceneLocalFileID);
		localMap_.insert_or_assign(previous, sceneLocalFileID);
	}
	data.addedEntityMap.clear();
	for (auto& added : data.addedEntities) {

		const UUID previous = added.sceneLocalFileID;
		if (!preserveLocalFileIDs) {
			added.sceneLocalFileID = context_.AllocateLocalFileID();
		}
		localMap.emplace(previous, added.sceneLocalFileID);
		localMap_.insert_or_assign(previous, added.sceneLocalFileID);
		data.addedEntityMap.emplace_back(previous, added.sceneLocalFileID);
	}
	for (const auto& [source, target] : externalMap) {
		localMap.try_emplace(source, target);
	}

	auto remapLocal = [&](UUID value) {
		auto it = localMap.find(value);
		return it != localMap.end() ? it->second : value;
		};
	data.rootParentSceneLocalFileID = remapLocal(data.rootParentSceneLocalFileID);
	for (auto& modification : data.modifications) {
		PrefabReferenceRemapper::RemapValue(modification.value, modification.path, localMap,
			PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
	}
	for (auto& addedComponent : data.addedComponents) {
		PrefabReferenceRemapper::RemapComponent(addedComponent.type, addedComponent.value, localMap,
			PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
	}
	for (auto& added : data.addedEntities) {

		added.parentSceneLocalFileID = remapLocal(added.parentSceneLocalFileID);
		PrefabReferenceRemapper::RemapComponents(added.components, localMap,
			PrefabReferenceRemapper::ReferenceSpace::Scene, data.prefabAsset);
	}
	for (auto& hierarchyMod : data.hierarchyModifications) {
		hierarchyMod.externalParentSceneLocalFileID =
			remapLocal(hierarchyMod.externalParentSceneLocalFileID);
	}

	data.instanceID = UUID::New();
	// 複製先の所有へ更新し、編集用の実体IDを持ち越さない
	data.ownerPrefabInstanceID = ownerInstanceID;
	data.stableUUIDMap.clear();
	for (auto& nested : data.nestedInstances) {
		FreshenData(nested, data.instanceID, localMap, preserveLocalFileIDs);
	}
}

void Engine::PrefabNestedInstanceBuilder::AppendRestoredMap(const PrefabInstanceData& declaration,
	const PrefabInstanceData& restored) {

	std::unordered_map<UUID, UUID> restoredEntityMap;
	// Prefab内IDを介して復元先のSceneローカルIDへ接続
	for (const auto& [prefabLocalFileID, sceneLocalFileID] : restored.entityMap) {
		restoredEntityMap.emplace(prefabLocalFileID, sceneLocalFileID);
	}
	for (const auto& [prefabLocalFileID, sceneLocalFileID] : declaration.entityMap) {
		auto it = restoredEntityMap.find(prefabLocalFileID);
		if (it != restoredEntityMap.end()) {
			localMap_.insert_or_assign(sceneLocalFileID, it->second);
		}
	}
	std::unordered_map<UUID, UUID> addedMap(restored.addedEntityMap.begin(), restored.addedEntityMap.end());
	std::unordered_set<UUID> restoredAddedIDs;
	for (const auto& added : restored.addedEntities) {
		restoredAddedIDs.insert(added.sceneLocalFileID);
	}
	for (const auto& added : declaration.addedEntities) {
		const UUID source = added.sceneLocalFileID;
		if (const auto target = addedMap.find(source); target != addedMap.end()) {
			localMap_.insert_or_assign(source, target->second);
		} else if (restoredAddedIDs.contains(source)) {
			// 同じ保存IDを維持した旧データだけはそのまま対応付ける
			localMap_.insert_or_assign(source, source);
		} else if (addedMap.empty() && !restoredAddedIDs.empty()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"[Prefab] 追加Entityの対応を特定できません Slot={} LocalFileID={}",
				ToString(declaration.nestedSlotID), ToString(source));
		}
	}
	for (const PrefabInstanceData& declarationNested : declaration.nestedInstances) {
		for (const PrefabInstanceData& restoredNested : restored.nestedInstances) {
			if (declarationNested.nestedSlotID == restoredNested.nestedSlotID) {
				AppendRestoredMap(declarationNested, restoredNested);
				break;
			}
		}
	}
}

bool Engine::PrefabNestedInstanceBuilder::Instantiate(PrefabInstanceData data,
	bool preserveSceneIDs, bool isPrefabAssetNested) {

	if (!preserveSceneIDs) {
		FreshenData(data, result_.prefabInstanceID,
			localMap_, desc_.preserveNestedLocalFileIDs);
	} else {
		data.ownerPrefabInstanceID = result_.prefabInstanceID;
	}
	data.isPrefabAssetNested = isPrefabAssetNested;

	const Entity nestedRoot = PrefabInstanceRebuilder::RebuildInstance(
		context_, data, desc_.ownerSceneInstanceID, desc_.nestedDepth + 1);
	if (!context_.world.IsAlive(nestedRoot)) {
		return false;
	}
	// ネスト以下を親の生成結果へまとめる
	for (const Entity& nestedEntity : HierarchyUtility::CollectLogicalSubtree(context_.world, nestedRoot)) {
		if (std::find(result_.createdEntities.begin(), result_.createdEntities.end(), nestedEntity) ==
			result_.createdEntities.end()) {
			result_.createdEntities.emplace_back(nestedEntity);
		}
	}
	return true;
}

bool Engine::PrefabNestedInstanceBuilder::Build(const nlohmann::json& fileJson) {

	std::vector<PrefabInstanceData> nestedDeclarations;
	std::unordered_set<UUID> declarationSlots;
	if (fileJson.contains("NestedPrefabInstances")) {
		if (!fileJson["NestedPrefabInstances"].is_array()) {
			return false;
		}
		for (const auto& nestedJson : fileJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			// 不正な宣言を欠落したまま公開しない
			if (!FromJson(nestedJson, nested) || !nested.nestedSlotID ||
				!declarationSlots.insert(nested.nestedSlotID).second) {
				return false;
			}
			nestedDeclarations.emplace_back(std::move(nested));
		}
	}

	std::unordered_set<UUID> restoredSlots;
	// 保存済みの差分を優先し、削除したSlotは復元しない
	bool nestedSucceeded = true;
	for (const auto& declaration : nestedDeclarations) {

		if (desc_.removedNestedSlots &&
			std::find(desc_.removedNestedSlots->begin(), desc_.removedNestedSlots->end(),
				declaration.nestedSlotID) != desc_.removedNestedSlots->end()) {
			continue;
		}
		if (const PrefabInstanceData* restored = FindRestored(declaration.nestedSlotID)) {
			restoredSlots.insert(declaration.nestedSlotID);
			AppendRestoredMap(declaration, *restored);
			nestedSucceeded = Instantiate(*restored, true, true);
		} else {
			nestedSucceeded = Instantiate(declaration, false, true);
		}
		if (!nestedSucceeded) {
			break;
		}
	}
	if (nestedSucceeded && desc_.nestedInstanceRemap) {
		for (const auto& restored : *desc_.nestedInstanceRemap) {

			if (restoredSlots.contains(restored.nestedSlotID)) {
				continue;
			}
			if (restored.isPrefabAssetNested) {
				continue;
			}
			if (!Instantiate(restored, true, false)) {
				nestedSucceeded = false;
				break;
			}
		}
	}
	if (!nestedSucceeded) {
		return false;
	}
	return true;
}
