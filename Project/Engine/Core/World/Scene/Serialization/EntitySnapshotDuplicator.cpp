#include "EntitySnapshotDuplicator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace {
	// コンポーネントからシーンオブジェクトのローカルファイルIDを読み取る
	Engine::UUID ReadLocalFileIDFromComponents(const nlohmann::json& components) {

		if (!components.contains("SceneObject")) {
			return Engine::UUID{};
		}

		const std::string localID = components["SceneObject"].value("localFileId", "");
		return localID.empty() ? Engine::UUID{} : Engine::FromString16Hex(localID);
	}
	// コンポーネントにシーンオブジェクトのローカルファイルIDを書き込む
	void WriteLocalFileIDToComponents(nlohmann::json& components, Engine::UUID localFileID) {

		if (!components.contains("SceneObject")) {
			components["SceneObject"] = nlohmann::json::object();
		}
		components["SceneObject"]["localFileId"] = localFileID ? Engine::ToString(localFileID) : "";
	}
	// コンポーネントから親のローカルファイルIDを読み取る
	Engine::UUID ReadParentLocalFileIDFromComponents(const nlohmann::json& components) {

		if (!components.contains("Hierarchy")) {
			return Engine::UUID{};
		}
		const std::string parentLocalID = components["Hierarchy"].value("parentLocalFileID", "");
		return parentLocalID.empty() ? Engine::UUID{} : Engine::FromString16Hex(parentLocalID);
	}
	// コンポーネントに親のローカルファイルIDを書き込む
	void WriteParentLocalFileIDToComponents(nlohmann::json& components, Engine::UUID parentLocalFileID) {

		if (!components.contains("Hierarchy")) {
			components["Hierarchy"] = nlohmann::json::object();
		}
		components["Hierarchy"]["parentLocalFileID"] = parentLocalFileID ? Engine::ToString(parentLocalFileID) : "";
	}
	// 保存IDからルートを特定して名前を変更する
	void WriteRootNameToSnapshot(Engine::EntityTreeSnapshot& snapshot, const std::string_view& name) {

		if (snapshot.IsEmpty()) {
			return;
		}
		const auto root = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
			[&](const auto& entity) { return entity.stableUUID == snapshot.rootStableUUID; });
		if (root == snapshot.entities.end()) {
			throw std::invalid_argument("複製対象のルートがありません");
		}
		root->components["Name"]["name"] = std::string(name);
	}
}

void Engine::EntitySnapshotDuplicator::Build(const EntityTreeSnapshot& source, const std::string_view& rootName,
	EntityTreeSnapshot& result) {

	EntityTreeSnapshot candidate;
	BuildCandidate(source, rootName, candidate);
	result = std::move(candidate);
}

void Engine::EntitySnapshotDuplicator::ClearRootParentLink(EntityTreeSnapshot& snapshot) {

	if (snapshot.IsEmpty()) {
		return;
	}
	const auto root = std::find_if(snapshot.entities.begin(), snapshot.entities.end(),
		[&](const auto& entity) { return entity.stableUUID == snapshot.rootStableUUID; });
	if (root == snapshot.entities.end()) {
		throw std::invalid_argument("親を解除するルートがありません");
	}
	WriteParentLocalFileIDToComponents(root->components, UUID{});
}

void Engine::EntitySnapshotDuplicator::BuildCandidate(const EntityTreeSnapshot& sourceSnapshot,
	const std::string_view& duplicatedRootName, EntityTreeSnapshot& outSnapshot) {

	outSnapshot.Clear();
	if (sourceSnapshot.IsEmpty()) {
		return;
	}

	std::unordered_map<UUID, UUID> stableUUIDMap;
	std::unordered_map<UUID, UUID> localFileIDMap;
	std::unordered_map<UUID, UUID> prefabInstanceIDMap;
	std::unordered_map<UUID, AssetID> referenceSources;
	outSnapshot.ownerSceneInstanceID = sourceSnapshot.ownerSceneInstanceID;
	outSnapshot.ownerSourceAsset = sourceSnapshot.ownerSourceAsset;
	stableUUIDMap.reserve(sourceSnapshot.entities.size());
	localFileIDMap.reserve(sourceSnapshot.entities.size());
	prefabInstanceIDMap.reserve(sourceSnapshot.entities.size());

	// 複製後に使用するUUID、ローカルファイルID、プレファブインスタンスIDを生成
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		if (!sourceEntity.stableUUID || !sourceEntity.components.is_object() ||
			!stableUUIDMap.emplace(sourceEntity.stableUUID, UUID::New()).second) {
			throw std::invalid_argument("複製元の実体IDまたはComponentデータが不正です");
		}
		if (sourceEntity.sceneInstanceID && sourceSnapshot.ownerSceneInstanceID &&
			sourceEntity.sceneInstanceID != sourceSnapshot.ownerSceneInstanceID) {
			throw std::invalid_argument("異なるSceneの実体を一つの階層として複製できません");
		}
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(sourceEntity.components);
		if (oldLocalFileID) {

			if (!localFileIDMap.emplace(oldLocalFileID, UUID::New()).second) {
				throw std::invalid_argument("複製元のLocalFileIDが重複しています");
			}
			referenceSources[oldLocalFileID] = sourceEntity.sourceAsset ?
				sourceEntity.sourceAsset : sourceSnapshot.ownerSourceAsset;
		}
		if (sourceEntity.components.contains("PrefabLink")) {

			const PrefabLinkComponent prefabLink =
				sourceEntity.components["PrefabLink"].get<PrefabLinkComponent>();
			if (prefabLink.isPrefabRoot && prefabLink.prefabInstanceID &&
				!prefabInstanceIDMap.contains(prefabLink.prefabInstanceID)) {

				prefabInstanceIDMap[prefabLink.prefabInstanceID] = UUID::New();
			}
		}
	}

	// UUIDのマッピングを作成した後で、ルートのStableUUIDを先に書き換えておく
	if (!stableUUIDMap.contains(sourceSnapshot.rootStableUUID)) {
		throw std::invalid_argument("複製元のルートが保存データにありません");
	}
	outSnapshot.rootStableUUID = stableUUIDMap.at(sourceSnapshot.rootStableUUID);
	outSnapshot.entities.reserve(sourceSnapshot.entities.size());

	// 別Assetの同じLocalFileIDへ接続しない
	const PrefabReferenceRemapper::EntityReferenceMapper remapReference = [&](nlohmann::json& reference) {

		PrefabReferenceRemapper::RemapMatchedEntityReference(reference, localFileIDMap, referenceSources);
	};

	// jsonを複製して参照IDを書き換える
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		SerializedEntitySnapshot duplicatedEntity = sourceEntity;
		duplicatedEntity.stableUUID = stableUUIDMap[sourceEntity.stableUUID];
		for (auto it = duplicatedEntity.components.begin(); it != duplicatedEntity.components.end(); ++it) {
			PrefabReferenceRemapper::RemapComponentReferences(it.key(), it.value(), localFileIDMap, remapReference);
		}

		// シーンオブジェクトのローカルフィールドIDを再生成
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(duplicatedEntity.components);
		if (oldLocalFileID && localFileIDMap.contains(oldLocalFileID)) {

			WriteLocalFileIDToComponents(duplicatedEntity.components, localFileIDMap.at(oldLocalFileID));
		}
		if (duplicatedEntity.components.contains("PrefabLink")) {

			PrefabLinkComponent prefabLink = duplicatedEntity.components["PrefabLink"].get<PrefabLinkComponent>();
			if (prefabLink.prefabInstanceID && prefabInstanceIDMap.contains(prefabLink.prefabInstanceID)) {

				prefabLink.prefabInstanceID = prefabInstanceIDMap.at(prefabLink.prefabInstanceID);
				prefabLink.savedInstanceID = {};
				// 追加Entityの宣言IDを残し、実体IDだけ複製先へ更新
				for (auto& [source, target] : prefabLink.addedEntityMap) {
					const auto replacement = localFileIDMap.find(target);
					target = replacement != localFileIDMap.end() ? replacement->second : UUID{};
				}
				std::erase_if(prefabLink.addedEntityMap, [](const auto& entry) { return !entry.second; });
				// 複製範囲外の所有者やスロットを新しいルートへ持ち込まない
				if (prefabInstanceIDMap.contains(prefabLink.ownerPrefabInstanceID)) {
					prefabLink.ownerPrefabInstanceID = prefabInstanceIDMap.at(prefabLink.ownerPrefabInstanceID);
				} else {
					prefabLink.ownerPrefabInstanceID = {};
					prefabLink.nestedSlotID = {};
					prefabLink.isPrefabAssetNested = false;
				}
				duplicatedEntity.components["PrefabLink"] = prefabLink;
			} else {
				// ルートを含まない部分複製は追加Entityとして扱う
				duplicatedEntity.components.erase("PrefabLink");
			}
		}

		// ヒエラルキーのローカルフィールドIDを内部複製用に張り替える
		if (duplicatedEntity.stableUUID == outSnapshot.rootStableUUID) {

			// ルートの親はコマンド側で外部親へ付けるので、いったん切る
			WriteParentLocalFileIDToComponents(duplicatedEntity.components, UUID{});
		} else {

			const UUID oldParentLocalID = ReadParentLocalFileIDFromComponents(sourceEntity.components);
			if (!oldParentLocalID || !localFileIDMap.contains(oldParentLocalID)) {

				WriteParentLocalFileIDToComponents(duplicatedEntity.components, UUID{});
			}
		}
		// スナップショットエンティティに追加
		outSnapshot.entities.emplace_back(std::move(duplicatedEntity));
	}
	WriteRootNameToSnapshot(outSnapshot, duplicatedRootName);
}
