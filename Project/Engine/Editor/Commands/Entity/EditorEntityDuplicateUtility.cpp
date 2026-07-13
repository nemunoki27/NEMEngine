#include "EditorEntityDuplicateUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>

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
	// スナップショットのルートの名前を変更しルートが存在しない場合は何もしない
	void WriteRootNameToSnapshot(Engine::EditorEntityTreeSnapshot& snapshot, const std::string_view& name) {

		if (snapshot.IsEmpty()) {
			return;
		}
		auto& root = snapshot.entities.front();
		if (!root.components.contains("Name")) {
			root.components["Name"] = nlohmann::json::object();
		}
		root.components["Name"]["name"] = std::string(name);
	}
	// エンティティとその子孫にシーンインスタンスIDとソースアセットを再帰的に設定する
	void PropagateSceneRuntimeStateRecursive(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::UUID sceneInstanceID, Engine::AssetID sourceAsset) {

		// エンティティが存在しない場合は何もしない
		if (!world.IsAlive(entity)) {
			return;
		}

		// シーンオブジェクトコンポーネントがある場合はシーンインスタンスIDとソースアセットを設定する
		if (world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);

			if (sceneInstanceID) {
				sceneObject.sceneInstanceID = sceneInstanceID;
			}
			if (sourceAsset) {
				sceneObject.sourceAsset = sourceAsset;
			}
		}
		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return;
		}

		// 子孫に対しても同じシーンインスタンスIDとソースアセットを設定
		Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
		while (child.IsValid() && world.IsAlive(child)) {

			PropagateSceneRuntimeStateRecursive(world, child, sceneInstanceID, sourceAsset);
			if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
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

	if (snapshot.IsEmpty()) {
		return;
	}
	WriteParentLocalFileIDToComponents(snapshot.entities.front().components, UUID{});
}

void Engine::EditorEntityDuplicateUtility::BuildDuplicateSnapshot(const EditorEntityTreeSnapshot& sourceSnapshot,
	const std::string_view& duplicatedRootName, EditorEntityTreeSnapshot& outSnapshot) {

	outSnapshot.Clear();
	if (sourceSnapshot.IsEmpty()) {
		return;
	}

	std::unordered_map<UUID, UUID> stableUUIDMap;
	std::unordered_map<UUID, UUID> localFileIDMap;
	std::unordered_map<UUID, UUID> prefabInstanceIDMap;
	stableUUIDMap.reserve(sourceSnapshot.entities.size());
	localFileIDMap.reserve(sourceSnapshot.entities.size());
	prefabInstanceIDMap.reserve(sourceSnapshot.entities.size());
	bool preservePrefabInstance = false;
	if (sourceSnapshot.entities.front().components.contains("PrefabLink")) {

		const PrefabLinkComponent rootPrefabLink =
			sourceSnapshot.entities.front().components["PrefabLink"].get<PrefabLinkComponent>();
		preservePrefabInstance = rootPrefabLink.isPrefabRoot;
	}

	// 複製後に使用するUUID、ローカルファイルID、プレファブインスタンスIDを生成
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		stableUUIDMap[sourceEntity.stableUUID] = UUID::New();
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(sourceEntity.components);
		if (oldLocalFileID) {

			localFileIDMap[oldLocalFileID] = UUID::New();
		}
		if (preservePrefabInstance && sourceEntity.components.contains("PrefabLink")) {

			const PrefabLinkComponent prefabLink =
				sourceEntity.components["PrefabLink"].get<PrefabLinkComponent>();
			if (prefabLink.isPrefabRoot && prefabLink.prefabInstanceID &&
				!prefabInstanceIDMap.contains(prefabLink.prefabInstanceID)) {

				prefabInstanceIDMap[prefabLink.prefabInstanceID] = UUID::New();
			}
		}
	}

	// UUIDのマッピングを作成した後で、ルートのStableUUIDを先に書き換えておく
	outSnapshot.rootStableUUID = stableUUIDMap[sourceSnapshot.rootStableUUID];
	outSnapshot.entities.reserve(sourceSnapshot.entities.size());

	// jsonを複製して参照IDを書き換える
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		SerializedEntitySnapshot duplicatedEntity{};
		duplicatedEntity.stableUUID = stableUUIDMap[sourceEntity.stableUUID];
		duplicatedEntity.components = sourceEntity.components;

		// シーンオブジェクトのローカルフィールドIDを再生成
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(duplicatedEntity.components);
		if (oldLocalFileID && localFileIDMap.contains(oldLocalFileID)) {

			WriteLocalFileIDToComponents(duplicatedEntity.components, localFileIDMap.at(oldLocalFileID));
		}
		if (!preservePrefabInstance) {

			// Prefabの子を複製した場合は追加Entityとして扱う
			duplicatedEntity.components.erase("PrefabLink");
		} else if (duplicatedEntity.components.contains("PrefabLink")) {

			PrefabLinkComponent prefabLink = duplicatedEntity.components["PrefabLink"].get<PrefabLinkComponent>();
			if (prefabLink.prefabInstanceID && prefabInstanceIDMap.contains(prefabLink.prefabInstanceID)) {

				prefabLink.prefabInstanceID = prefabInstanceIDMap.at(prefabLink.prefabInstanceID);
				duplicatedEntity.components["PrefabLink"] = prefabLink;
			}
		}

		// ヒエラルキーのローカルフィールドIDを内部複製用に張り替える
		if (duplicatedEntity.stableUUID == outSnapshot.rootStableUUID) {

			// ルートの親はコマンド側で外部親へ付けるので、いったん切る
			WriteParentLocalFileIDToComponents(duplicatedEntity.components, UUID{});
		} else {

			const UUID oldParentLocalID = ReadParentLocalFileIDFromComponents(duplicatedEntity.components);
			if (oldParentLocalID && localFileIDMap.contains(oldParentLocalID)) {

				WriteParentLocalFileIDToComponents(duplicatedEntity.components, localFileIDMap.at(oldParentLocalID));
			} else {

				WriteParentLocalFileIDToComponents(duplicatedEntity.components, UUID{});
			}
		}
		// スナップショットエンティティに追加
		outSnapshot.entities.emplace_back(std::move(duplicatedEntity));
	}
	WriteRootNameToSnapshot(outSnapshot, duplicatedRootName);
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

		PropagateSceneRuntimeStateRecursive(world, root, resolvedSceneInstanceID, resolvedSourceAsset);
	}

	return root;
}
