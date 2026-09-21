#include "PrefabInstantiator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabOwnership.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceRebuilder.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace Engine::PrefabDocument;

namespace {

	constexpr uint32_t kMaximumNestedPrefabDepth = 32;

}

bool Engine::PrefabInstantiator::InstantiatePrefab(PrefabGenerationContext& context, AssetID prefabAsset,
	PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) {

	AssetDatabase& database = context.database;
	HierarchySystem& hierarchySystem = context.hierarchySystem;
	ECSWorld& world = context.world;

	// 空にする
	outResult = PrefabInstantiateResult{};

	std::filesystem::path fullPath;
	nlohmann::json fileJson;
	if (!PrefabDocument::Read(database, prefabAsset, fullPath, fileJson)) {
		return false;
	}
	if (desc.nestedDepth > kMaximumNestedPrefabDepth) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] ネストPrefabの深度上限を超えました AssetID={}", ToString(prefabAsset));
		return false;
	}
	PrefabReferenceRemapper::NormalizePrefabFileHierarchy(fileJson);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ファイルからプレファブ読み込み
	PrefabHeader header{};
	if (!FromJson(fileJson["Header"], header)) {
		return false;
	}
	header.guid = prefabAsset;
	if (!header.rootLocalFileID) {

		const bool hasNestedPrefab = fileJson.contains("NestedPrefabInstances") &&
			(!fileJson["NestedPrefabInstances"].is_array() || !fileJson["NestedPrefabInstances"].empty());
		if (!fileJson["Entities"].empty() || hasNestedPrefab) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] Prefabのルート情報が不正です AssetID={}", ToString(prefabAsset));
			return false;
		}

		outResult.prefabInstanceID = desc.forcedInstanceID ? desc.forcedInstanceID : UUID::New();
		const std::string rootName = header.name.empty() ? "NewPrefab" : header.name;
		outResult.root = SceneAuthoring::CreateGameObject(world, rootName);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(outResult.root);
		sceneObject.localFileID = AllocateUniqueLocalFileID(world);
		sceneObject.sourceAsset = prefabAsset;
		sceneObject.sceneInstanceID = desc.ownerSceneInstanceID;
		PrefabOwnership::SetPrefabLink(world, outResult.root, prefabAsset, sceneObject.localFileID,
			outResult.prefabInstanceID, true, desc.ownerPrefabInstanceID,
			desc.nestedSlotID, desc.isPrefabAssetNested);
		outResult.createdEntities.emplace_back(outResult.root);
		outResult.sourceLocalToEntity.emplace(sceneObject.localFileID, outResult.root);
		if (world.IsAlive(desc.parent)) {
			hierarchySystem.SetParent(world, outResult.root, desc.parent);
		}
		return true;
	}

	std::unordered_set<UUID> prefabLocalFileIDs;
	for (const auto& entityJson : fileJson["Entities"]) {

		const UUID localFileID = ReadEntityLocalFileID(entityJson);
		if (!localFileID || !entityJson.contains("Components") ||
			!entityJson["Components"].is_object() ||
			!prefabLocalFileIDs.insert(localFileID).second) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[PrefabSystem] Prefab内のEntity情報が不正です AssetID={}", ToString(prefabAsset));
			return false;
		}
		for (auto it = entityJson["Components"].begin(); it != entityJson["Components"].end(); ++it) {

			if (it.key() == "JointAttachment") {
				continue;
			}
			if (!ComponentTypeRegistry::GetInstance().FindByName(it.key())) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[PrefabSystem] 未登録のComponentTypeです AssetID={} Component={}",
					ToString(prefabAsset), it.key());
				return false;
			}
		}
	}
	if (fileJson.contains("NestedPrefabInstances")) {

		if (!fileJson["NestedPrefabInstances"].is_array()) {
			return false;
		}
		std::unordered_set<UUID> nestedSlotIDs;
		for (const auto& nestedJson : fileJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			if (!FromJson(nestedJson, nested) || !nested.nestedSlotID ||
				!nestedSlotIDs.insert(nested.nestedSlotID).second) {

				Logger::Output(LogType::Engine, spdlog::level::err,
					"[PrefabSystem] ネストPrefab情報が不正です AssetID={}", ToString(prefabAsset));
				return false;
			}
		}
	}
	if (!prefabLocalFileIDs.contains(header.rootLocalFileID)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[PrefabSystem] PrefabのルートEntityが存在しません AssetID={}", ToString(prefabAsset));
		return false;
	}

	// シーン内で一意なプレファブインスタンスIDを生成、薄い保存からの復元では指定IDを使い同一性を保つ
	outResult.prefabInstanceID = desc.forcedInstanceID ? desc.forcedInstanceID : UUID::New();

	// プレファブ内ローカルIDから復元すべきシーンローカルIDを引くための一時マップ
	std::unordered_map<UUID, UUID> remapLookup;
	if (desc.localFileIDRemap) {
		for (const auto& pair : *desc.localFileIDRemap) {
			remapLookup.emplace(pair.first, pair.second);
		}
	}
	std::unordered_map<UUID, UUID> stableUUIDLookup;
	if (desc.stableUUIDRemap) {
		for (const auto& pair : *desc.stableUUIDRemap) {
			stableUUIDLookup.emplace(pair.first, pair.second);
		}
	}

	std::unordered_map<UUID, UUID> prefabLocalToSceneLocal;
	std::vector<std::pair<Entity, const nlohmann::json*>> pendingLoads;
	pendingLoads.reserve(fileJson["Entities"].size());

	//============================================================================
	//	エンティティを作成し、ローカルからシーンのIDへのマップを構築する
	//============================================================================
	for (const auto& entityJson : fileJson["Entities"]) {

		// プレファブファイル内のローカルIDを読み取る
		UUID prefabLocalFileID = ReadEntityLocalFileID(entityJson);

		// 最終シグネチャで作成しPrefab生成中の構造移動を抑える
		std::vector<uint32_t> componentTypeIDs;
		const auto& prefabComponents = entityJson["Components"];
		componentTypeIDs.reserve(prefabComponents.size() + 1);
		componentTypeIDs.emplace_back(
			ComponentTypeRegistry::GetInstance().GetID<PrefabLinkComponent>());
		for (auto it = prefabComponents.begin(); it != prefabComponents.end(); ++it) {

			// JointAttachmentは参照先を解決できたエンティティだけ後から追加する
			if (it.key() == "JointAttachment") {
				continue;
			}
			const ComponentTypeInfo* info =
				ComponentTypeRegistry::GetInstance().FindByName(it.key());
			if (!info) {
				return false;
			}
			componentTypeIDs.emplace_back(info->id);
		}
		UUID stableUUID{};
		if (auto stableIt = stableUUIDLookup.find(prefabLocalFileID);
			stableIt != stableUUIDLookup.end()) {
			stableUUID = stableIt->second;
		}
		const Entity entity = SceneAuthoring::CreateGameObject(
			world, "Entity", componentTypeIDs, stableUUID);
		// 復元時は保存済みのシーンローカルIDを使い、無ければ新規採番する
		UUID newSceneLocalFileID{};
		if (auto remapIt = remapLookup.find(prefabLocalFileID); remapIt != remapLookup.end() && remapIt->second) {
			newSceneLocalFileID = remapIt->second;
		} else {
			newSceneLocalFileID = AllocateUniqueLocalFileID(world);
		}
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {
			const auto& components = entityJson["Components"];
			if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
				world.AddComponentFromJson(entity, "SceneObject", components["SceneObject"]);
			}
		}
		// シーンオブジェクト初期化
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = newSceneLocalFileID;
			sceneObject.sourceAsset = prefabAsset;
			sceneObject.sceneInstanceID = desc.ownerSceneInstanceID;
		}
		// プレファブリンク初期化
		PrefabOwnership::SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID,
			outResult.prefabInstanceID, prefabLocalFileID == header.rootLocalFileID,
			desc.ownerPrefabInstanceID, desc.nestedSlotID, desc.isPrefabAssetNested);

		// 作成したエンティティを結果に追加
		outResult.createdEntities.emplace_back(entity);
		outResult.sourceLocalToEntity.emplace(prefabLocalFileID, entity);
		prefabLocalToSceneLocal.emplace(prefabLocalFileID, newSceneLocalFileID);
		pendingLoads.emplace_back(entity, &entityJson);
	}

	//============================================================================
	//	コンポーネントを追加する
	//============================================================================
	for (auto& [entity, entityJson] : pendingLoads) {

		if (!entityJson->contains("Components") || !(*entityJson)["Components"].is_object()) {
			continue;
		}

		const auto& components = (*entityJson)["Components"];
		for (auto it = components.begin(); it != components.end(); ++it) {

			const std::string& typeName = it.key();
			if (typeName == "SceneObject" || typeName == "PrefabLink") {
				continue;
			}
			nlohmann::json data = it.value();
			if (typeName == "JointAttachment") {

				const UUID entityLocalFileID = ReadEntityLocalFileID(*entityJson);
				if (entityLocalFileID == header.rootLocalFileID ||
					!HasPrefabLocalJointTarget(data, prefabLocalToSceneLocal)) {
					continue;
				}
			}
			PrefabReferenceRemapper::RemapComponent(
				typeName, data, prefabLocalToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, prefabAsset);
			world.AddComponentFromJson(entity, typeName, data);
		}
	}

	//============================================================================
	//	MeshRendererのサブメッシュをmesh実体へ正規化する
	//============================================================================
	for (const Entity& entity : outResult.createdEntities) {

		if (!world.IsAlive(entity) || !world.HasComponent<MeshRendererComponent>(entity)) {
			continue;
		}
		MeshSubMeshAuthoring::SyncEntity(&database, world, entity, true);
	}

	//============================================================================
	//	ヒエラルキーの親参照をローカルIDからシーンのIDへ変換する
	//============================================================================
	for (const Entity& entity : outResult.createdEntities) {

		if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}

		auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
		if (!hierarchy.parentLocalFileID) {
			continue;
		}
		auto it = prefabLocalToSceneLocal.find(hierarchy.parentLocalFileID);
		if (it != prefabLocalToSceneLocal.end()) {
			hierarchy.parentLocalFileID = it->second;
		}
	}

	// ランタイムのリンクを再構築する
	hierarchySystem.RebuildRuntimeLinks(world, outResult.createdEntities);

	// ルートエンティティを特定する
	auto rootIt = outResult.sourceLocalToEntity.find(header.rootLocalFileID);
	if (rootIt != outResult.sourceLocalToEntity.end()) {

		outResult.root = rootIt->second;
	}
	// ルートが特定できなかった場合は、作成されたエンティティの先頭をルートとする
	else if (!outResult.createdEntities.empty()) {

		outResult.root = outResult.createdEntities.front();
	}

	// 新規生成のときだけルート名を.prefabのベース名にする、シーン復元では保存済みの名前を尊重する
	if (desc.renameRootToPrefabName && world.IsAlive(outResult.root) &&
		world.HasComponent<NameComponent>(outResult.root)) {

		// プレファブは ".prefab.json" の二重拡張子なので、stemを二段かけて純粋な名前を取り出す
		std::filesystem::path namePath = fullPath.stem();
		if (namePath.extension() == ".prefab") {
			namePath = namePath.stem();
		}
		const std::string baseName = Algorithm::PathToUTF8(namePath);
		if (!baseName.empty()) {

			// 同名インスタンスがあれば name_N へずらす、生成中のルート自身は判定から外す
			world.GetComponent<NameComponent>(outResult.root).name =
				SceneAuthoring::MakeUniqueEntityName(world, baseName, outResult.root);
		}
	}

	// Prefabの実体を単一ルートの階層へ揃える
	if (outResult.root.IsValid()) {
		for (const Entity& entity : outResult.createdEntities) {

			if (entity == outResult.root || !world.IsAlive(entity)) {
				continue;
			}
			// ジョイント接続は論理的にルート配下なので通常の親子関係を重ねない
			if (world.HasComponent<JointAttachmentComponent>(entity)) {
				continue;
			}
			const bool isRoot = !world.HasComponent<HierarchyComponent>(entity) ||
				!world.IsAlive(world.GetComponent<HierarchyComponent>(entity).parent);
			if (isRoot) {
				hierarchySystem.SetParent(world, entity, outResult.root);
			}
		}
	}

	// 親が指定されている場合は、ルートを親にぶら下げる
	if (world.IsAlive(desc.parent) && outResult.root.IsValid()) {

		hierarchySystem.SetParent(world, outResult.root, desc.parent);
	}

	// 親Prefabアセットに保存されたネストPrefabを差分付きで生成する
	const PrefabReferenceRemapper::LocalFileIDMap directPrefabLocalToSceneLocal =
		prefabLocalToSceneLocal;
	std::vector<PrefabInstanceData> nestedDeclarations;
	if (fileJson.contains("NestedPrefabInstances") && fileJson["NestedPrefabInstances"].is_array()) {
		for (const auto& nestedJson : fileJson["NestedPrefabInstances"]) {

			PrefabInstanceData nested{};
			if (FromJson(nestedJson, nested)) {
				nestedDeclarations.emplace_back(std::move(nested));
			}
		}
	}

	std::unordered_set<UUID> restoredSlots;
	auto findRestoredNested = [&](UUID nestedSlotID) -> const PrefabInstanceData* {

		if (!desc.nestedInstanceRemap) {
			return nullptr;
		}
		for (const auto& nested : *desc.nestedInstanceRemap) {
			if (nested.nestedSlotID == nestedSlotID) {
				return &nested;
			}
		}
		return nullptr;
		};

	auto freshenNestedData = [&](auto&& self, PrefabInstanceData& data, UUID ownerInstanceID,
		const PrefabReferenceRemapper::LocalFileIDMap& externalMap, bool preserveLocalFileIDs) -> void {

		PrefabReferenceRemapper::LocalFileIDMap localMap;
		for (auto& [prefabLocalFileID, sceneLocalFileID] : data.entityMap) {

			const UUID previous = sceneLocalFileID;
			if (!preserveLocalFileIDs) {
				sceneLocalFileID = AllocateUniqueLocalFileID(world);
			}
			localMap.emplace(previous, sceneLocalFileID);
			prefabLocalToSceneLocal.insert_or_assign(previous, sceneLocalFileID);
		}
		for (auto& added : data.addedEntities) {

			const UUID previous = added.sceneLocalFileID;
			if (!preserveLocalFileIDs) {
				added.sceneLocalFileID = AllocateUniqueLocalFileID(world);
			}
			localMap.emplace(previous, added.sceneLocalFileID);
			prefabLocalToSceneLocal.insert_or_assign(previous, added.sceneLocalFileID);
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
		data.ownerPrefabInstanceID = ownerInstanceID;
		data.stableUUIDMap.clear();
		for (auto& nested : data.nestedInstances) {
			self(self, nested, data.instanceID, localMap, preserveLocalFileIDs);
		}
		};

	auto appendRestoredNestedMap = [&](auto&& self, const PrefabInstanceData& declaration,
		const PrefabInstanceData& restored) -> void {

		std::unordered_map<UUID, UUID> restoredEntityMap;
		for (const auto& [prefabLocalFileID, sceneLocalFileID] : restored.entityMap) {
			restoredEntityMap.emplace(prefabLocalFileID, sceneLocalFileID);
		}
		for (const auto& [prefabLocalFileID, sceneLocalFileID] : declaration.entityMap) {
			auto it = restoredEntityMap.find(prefabLocalFileID);
			if (it != restoredEntityMap.end()) {
				prefabLocalToSceneLocal.insert_or_assign(sceneLocalFileID, it->second);
			}
		}
		const size_t addedCount = std::min(
			declaration.addedEntities.size(), restored.addedEntities.size());
		for (size_t i = 0; i < addedCount; ++i) {
			prefabLocalToSceneLocal.insert_or_assign(
				declaration.addedEntities[i].sceneLocalFileID,
				restored.addedEntities[i].sceneLocalFileID);
		}
		for (const PrefabInstanceData& declarationNested : declaration.nestedInstances) {
			for (const PrefabInstanceData& restoredNested : restored.nestedInstances) {
				if (declarationNested.nestedSlotID == restoredNested.nestedSlotID) {
					self(self, declarationNested, restoredNested);
					break;
				}
			}
		}
		};

	auto instantiateNested = [&](PrefabInstanceData data, bool preserveSceneIDs, bool isPrefabAssetNested) {

		if (!preserveSceneIDs) {
			freshenNestedData(freshenNestedData, data, outResult.prefabInstanceID,
				prefabLocalToSceneLocal, desc.preserveNestedLocalFileIDs);
		} else {
			data.ownerPrefabInstanceID = outResult.prefabInstanceID;
		}
		data.isPrefabAssetNested = isPrefabAssetNested;

		const Entity nestedRoot = PrefabInstanceRebuilder::RebuildInstance(
			context, data, desc.ownerSceneInstanceID, desc.nestedDepth + 1);
		if (!world.IsAlive(nestedRoot)) {
			return false;
		}
		for (const Entity& nestedEntity : HierarchyUtility::CollectLogicalSubtree(world, nestedRoot)) {
			if (std::find(outResult.createdEntities.begin(), outResult.createdEntities.end(), nestedEntity) ==
				outResult.createdEntities.end()) {
				outResult.createdEntities.emplace_back(nestedEntity);
			}
		}
		return true;
		};

	bool nestedSucceeded = true;
	for (const auto& declaration : nestedDeclarations) {

		if (desc.removedNestedSlots &&
			std::find(desc.removedNestedSlots->begin(), desc.removedNestedSlots->end(),
				declaration.nestedSlotID) != desc.removedNestedSlots->end()) {
			continue;
		}
		if (const PrefabInstanceData* restored = findRestoredNested(declaration.nestedSlotID)) {
			restoredSlots.insert(declaration.nestedSlotID);
			appendRestoredNestedMap(appendRestoredNestedMap, declaration, *restored);
			nestedSucceeded = instantiateNested(*restored, true, true);
		} else {
			nestedSucceeded = instantiateNested(declaration, false, true);
		}
		if (!nestedSucceeded) {
			break;
		}
	}
	if (nestedSucceeded && desc.nestedInstanceRemap) {
		for (const auto& restored : *desc.nestedInstanceRemap) {

			if (restoredSlots.contains(restored.nestedSlotID)) {
				continue;
			}
			if (restored.isPrefabAssetNested) {
				continue;
			}
			if (!instantiateNested(restored, true, false)) {
				nestedSucceeded = false;
				break;
			}
		}
	}
	if (!nestedSucceeded) {

		for (auto it = outResult.createdEntities.rbegin(); it != outResult.createdEntities.rend(); ++it) {
			if (world.IsAlive(*it)) {
				world.DestroyEntity(*it);
			}
		}
		world.FlushPendingDestroyEntities();
		outResult = PrefabInstantiateResult{};
		return false;
	}

	// ネスト先を指す参照だけを、全IDが確定した状態でもう一度読み込む
	for (auto& [entity, entityJson] : pendingLoads) {

		if (!world.IsAlive(entity) || !entityJson->contains("Components") ||
			!(*entityJson)["Components"].is_object()) {
			continue;
		}
		const auto& components = (*entityJson)["Components"];
		for (auto it = components.begin(); it != components.end(); ++it) {

			const std::string& typeName = it.key();
			if (typeName == "SceneObject" || typeName == "PrefabLink") {
				continue;
			}
			nlohmann::json directData = it.value();
			nlohmann::json completeData = it.value();
			PrefabReferenceRemapper::RemapComponent(typeName, directData,
				directPrefabLocalToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, prefabAsset);
			PrefabReferenceRemapper::RemapComponent(typeName, completeData,
				prefabLocalToSceneLocal, PrefabReferenceRemapper::ReferenceSpace::Scene, prefabAsset);
			if (directData == completeData) {
				continue;
			}
			world.AddComponentFromJson(entity, typeName, completeData);
		}
	}
	return true;
}

Engine::UUID Engine::PrefabInstantiator::AllocateUniqueLocalFileID(ECSWorld& world) {

	while (true) {

		UUID candidate = UUID::New();
		bool exists = false;
		// このローカルIDがすでにシーン内のどこかで使われていないか走査する
		world.ForEach<SceneObjectComponent>([&](const Entity&, SceneObjectComponent& sceneObject) {
			if (sceneObject.localFileID == candidate) {
				exists = true;
			}
			});
		if (!exists) {
			return candidate;
		}
	}
}

bool Engine::PrefabInstantiator::InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) {

	PrefabGenerationContext context{ database, hierarchySystem, world };
	return InstantiatePrefab(context, prefabAsset, outResult, desc);
}
