#include "EditorEntityDuplicateUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

//============================================================================
//	EditorEntityDuplicateUtility classMethods
//============================================================================

namespace {

	// 名前がEntity_Nの形式かを判定し、そうであればベース名とインデックスを分割する
	bool TryParseIndexedName(const std::string& name, std::string& outBase, uint32_t& outIndex) {

		const size_t pos = name.rfind('_');
		if (pos == std::string::npos || pos == 0 || pos + 1 >= name.size()) {
			return false;
		}
		for (size_t i = pos + 1; i < name.size(); ++i) {
			if (!std::isdigit(static_cast<unsigned char>(name[i]))) {

				return false;
			}
		}
		outBase = name.substr(0, pos);
		outIndex = static_cast<uint32_t>(std::stoul(name.substr(pos + 1)));
		return true;
	}
	// Entity_Nの形式の名前であればEntityを返し、そうでなければそのまま返す
	std::string NormalizeDuplicateBaseName(const std::string_view& sourceName) {

		std::string name = sourceName.empty() ? "Entity" : std::string(sourceName);

		std::string base;
		uint32_t index = 0;
		if (TryParseIndexedName(name, base, index)) {
			return base.empty() ? name : base;
		}
		return name;
	}
	// コンポーネントからシーンオブジェクトのローカルファイルIDを読み取る
	Engine::UUID ReadLocalFileIDFromComponents(const nlohmann::json& components) {

		if (!components.contains("SceneObject")) {
			return Engine::UUID{};
		}

		const std::string localID = components["SceneObject"].value("localFileID", "");
		return localID.empty() ? Engine::UUID{} : Engine::FromString16Hex(localID);
	}
	// コンポーネントにシーンオブジェクトのローカルファイルIDを書き込む
	void WriteLocalFileIDToComponents(nlohmann::json& components, Engine::UUID localFileID) {

		if (!components.contains("SceneObject")) {
			components["SceneObject"] = nlohmann::json::object();
		}
		components["SceneObject"]["localFileID"] = localFileID ? Engine::ToString(localFileID) : "";
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
	// スナップショットのルートの名前を変更する。ルートが存在しない場合は何もしない
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

	// ルート名からEntity_Nの形式のベース名を抽出する
	const std::string baseName = NormalizeDuplicateBaseName(sourceName);
	std::unordered_set<uint32_t> usedIndices;
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<NameComponent>(entity)) {
			return;
		}

		// 名前がEntity_Nの形式でベース名が同じものがあればインデックスを記録する
		const std::string& currentName = world.GetComponent<NameComponent>(entity).name;
		if (currentName == baseName) {
			usedIndices.insert(0);
			return;
		}

		// インデックスを解析してベース名が同じものがあればインデックスを記録する
		std::string currentBase;
		uint32_t currentIndex = 0;
		if (TryParseIndexedName(currentName, currentBase, currentIndex) && currentBase == baseName) {
			usedIndices.insert(currentIndex);
		}
		});
	// 1から始まる最小の未使用インデックスを見つけてベース名に付加する
	uint32_t candidate = 1;
	while (usedIndices.contains(candidate)) {
		++candidate;
	}
	return baseName + "_" + std::to_string(candidate);
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

	std::unordered_map<UUID, UUID> stableUuidMap;
	std::unordered_map<UUID, UUID> localFileIDMap;
	stableUuidMap.reserve(sourceSnapshot.entities.size());
	localFileIDMap.reserve(sourceSnapshot.entities.size());

	// 複製後に使用するUUID、ローカルファイルIDを生成
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		stableUuidMap[sourceEntity.stableUUID] = UUID::New();
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(sourceEntity.components);
		if (oldLocalFileID) {

			localFileIDMap[oldLocalFileID] = UUID::New();
		}
	}

	// UUIDのマッピングを作成した後で、ルートのStableUUIDを先に書き換えておく
	outSnapshot.rootStableUUID = stableUuidMap[sourceSnapshot.rootStableUUID];
	outSnapshot.entities.reserve(sourceSnapshot.entities.size());

	// jsonを複製して参照IDを書き換える
	for (const auto& sourceEntity : sourceSnapshot.entities) {

		SerializedEntitySnapshot duplicatedEntity{};
		duplicatedEntity.stableUUID = stableUuidMap[sourceEntity.stableUUID];
		duplicatedEntity.components = sourceEntity.components;

		// シーンオブジェクトのローカルフィールドIDを再生成
		const UUID oldLocalFileID = ReadLocalFileIDFromComponents(duplicatedEntity.components);
		if (oldLocalFileID && localFileIDMap.contains(oldLocalFileID)) {

			WriteLocalFileIDToComponents(duplicatedEntity.components, localFileIDMap.at(oldLocalFileID));
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

		// 外部親のEntityをUUIDから検索する。見つかって生きているならルートの親にする
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