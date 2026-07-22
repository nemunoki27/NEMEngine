#include "PrefabSystem.h"

//============================================================================
//	include
//============================================================================
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
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

//============================================================================
//	PrefabSystem classMethods
//============================================================================
namespace {

	// JSON値からローカルIDを読み取る
	static Engine::UUID ReadLocalFileID(const nlohmann::json& value) {

		Engine::UUID result{};
		if (value.is_string()) {
			const std::string raw = value.get<std::string>();
			return raw.empty() ? Engine::UUID{} : Engine::FromString16Hex(raw);
		}
		if (value.is_number_unsigned()) {
			result.value = value.get<uint64_t>();
		} else if (value.is_number_integer()) {

			const int64_t raw = value.get<int64_t>();
			if (raw > 0) {
				result.value = static_cast<uint64_t>(raw);
			}
		}
		return result;
	}

	// プレファブファイル内でエンティティを識別するためのIDを読み取る
	static Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson) {

		if (entityJson.contains("LocalFileID")) {
			const Engine::UUID localFileID = ReadLocalFileID(entityJson["LocalFileID"]);
			return localFileID ? localFileID : Engine::UUID::New();
		}
		if (entityJson.contains("UUID")) {
			const Engine::UUID localFileID = ReadLocalFileID(entityJson["UUID"]);
			return localFileID ? localFileID : Engine::UUID::New();
		}
		return Engine::UUID::New();
	}

	// ジョイント参照が同じプレファブ内に存在するか判定する
	static bool HasPrefabLocalJointTarget(const nlohmann::json& component,
		const Engine::PrefabReferenceRemapper::LocalFileIDMap& prefabLocalToSceneLocal) {

		if (!component.is_object() || !component.contains("skinnedEntityLocalFileID")) {
			return false;
		}
		const Engine::UUID targetLocalFileID = ReadLocalFileID(component["skinnedEntityLocalFileID"]);
		return targetLocalFileID && prefabLocalToSceneLocal.contains(targetLocalFileID);
	}

	// 保存時に使うPrefab内ローカルIDを解決する
	static Engine::UUID ResolvePrefabLocalFileID(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::AssetID prefabAsset) {

		if (world.HasComponent<Engine::PrefabLinkComponent>(entity)) {

			const auto& prefabLink = world.GetComponent<Engine::PrefabLinkComponent>(entity);
			if (prefabLink.prefabAsset == prefabAsset && prefabLink.prefabLocalFileID) {
				return prefabLink.prefabLocalFileID;
			}
		}
		if (!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return Engine::UUID{};
		}
		return world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
	}

	// SceneローカルIDからPrefabローカルIDへの変換表を作る
	static Engine::PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
		Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities, Engine::AssetID prefabAsset) {

		Engine::PrefabReferenceRemapper::LocalFileIDMap result;
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity) ||
				!world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				continue;
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			const auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			const Engine::UUID prefabLocalFileID = ResolvePrefabLocalFileID(world, entity, prefabAsset);
			if (sceneObject.localFileID && prefabLocalFileID) {
				result.emplace(sceneObject.localFileID, prefabLocalFileID);
			}
		}
		return result;
	}
}

void Engine::PrefabSystem::SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
	UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot) const {

	if (!world.IsAlive(entity)) {
		return;
	}
	auto& prefabLink = world.HasComponent<PrefabLinkComponent>(entity) ?
		world.GetComponent<PrefabLinkComponent>(entity) :
		world.AddComponent<PrefabLinkComponent>(entity);
	prefabLink.prefabAsset = prefabAsset;
	prefabLink.prefabLocalFileID = prefabLocalFileID;
	prefabLink.prefabInstanceID = prefabInstanceID;
	prefabLink.isPrefabRoot = isPrefabRoot;
}

Engine::UUID Engine::PrefabSystem::SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root,
	AssetID prefabAsset, UUID prefabInstanceID) const {

	if (!world.IsAlive(root) || !prefabAsset) {
		return UUID{};
	}
	const UUID resolvedInstanceID = prefabInstanceID ? prefabInstanceID : UUID::New();
	for (const Entity& entity : HierarchyUtility::CollectLogicalSubtree(world, root)) {

		if (!world.HasComponent<SceneObjectComponent>(entity)) {
			continue;
		}
		const UUID prefabLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
		SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID, resolvedInstanceID, entity == root);
	}
	return resolvedInstanceID;
}

bool Engine::PrefabSystem::SavePrefab(AssetDatabase& database, ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath) const {

	// ルートエンティティが存在するか
	if (!world.IsAlive(root)) {
		return false;
	}
	// rootのサブツリーを集めて保存する
	const std::vector<Entity> subtree = HierarchyUtility::CollectLogicalSubtree(world, root);
	return SavePrefabFromEntities(database, world, root, subtree, prefabAssetPath);
}

bool Engine::PrefabSystem::SavePrefabFromEntities(AssetDatabase& database, ECSWorld& world, const Entity& root,
	const std::vector<Entity>& entities, const std::string& prefabAssetPath) const {

	if (!world.IsAlive(root) || entities.empty()) {
		return false;
	}

	// プレファブアセットを登録
	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);

	// ルート情報をデフォルト構築
	SceneAuthoring::EnsureGameObjectDefaults(world, root);
	const PrefabReferenceRemapper::LocalFileIDMap sceneToPrefabLocal =
		BuildSceneToPrefabLocalMap(world, entities, prefabAsset);

	const UUID rootLocalFileID = ResolvePrefabLocalFileID(world, root, prefabAsset);

	// プレファブファイルの構築
	PrefabHeader header{};
	header.guid = prefabAsset;
	header.name = BuildDefaultPrefabName(world, root, prefabAssetPath);
	header.rootLocalFileID = rootLocalFileID;
	header.version = 1;

	nlohmann::json fileJson = nlohmann::json::object();

	fileJson["Header"] = ToJson(header);
	fileJson["Entities"] = nlohmann::json::array();

	// エンティティごとにコンポーネントをシリアライズしてファイルのnlohmann::jsonに追加
	for (const Entity& entity : entities) {

		if (!world.IsAlive(entity) ||
			!world.HasComponent<SceneObjectComponent>(entity)) {
			continue;
		}

		// デフォルトのコンポーネントを構築
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		const UUID prefabLocalFileID = ResolvePrefabLocalFileID(world, entity, prefabAsset);

		// エンティティ
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(prefabLocalFileID ? prefabLocalFileID : sceneObject.localFileID);

		// コンポーネント
		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		if (components.contains("SceneObject") && components["SceneObject"].is_object()) {
			components["SceneObject"]["localFileId"] = (prefabLocalFileID ? ToString(prefabLocalFileID) : ToString(sceneObject.localFileID));
		}
		// プレファブルートまたは保存対象外を指すジョイント参照は持ち込まない
		if (world.HasComponent<JointAttachmentComponent>(entity)) {

			const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
			if (entity == root || !sceneToPrefabLocal.contains(attachment.skinnedEntityLocalFileID)) {
				components.erase("JointAttachment");
			}
		}
		PrefabReferenceRemapper::RemapComponents(components, sceneToPrefabLocal, PrefabReferenceRemapper::ReferenceSpace::Prefab, prefabAsset);

		// プレファブ自体の中に、別プレファブ由来情報は持ち込まない
		components.erase("PrefabLink");

		// コンポーネントをエンティティに追加
		entityJson["Components"] = std::move(components);
		fileJson["Entities"].push_back(std::move(entityJson));
	}
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ファイルに保存
	std::filesystem::path savePath = database.ResolveAssetPath(prefabAssetPath);
	if (savePath.empty()) {
		savePath = prefabAssetPath;
	}
	JsonAdapter::Save(savePath.string(), fileJson);
	return true;
}

bool Engine::PrefabSystem::InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	// 空にする
	outResult = PrefabInstantiateResult{};

	// プレファブアセットが存在するか
	auto fullPath = database.ResolveFullPath(prefabAsset);
	if (fullPath.empty()) {
		return false;
	}

	// ファイルからnlohmann::json読み込み
	nlohmann::json fileJson = JsonAdapter::Load(fullPath.string(), true);
	if (!fileJson.is_object() || !fileJson.contains("Entities") || !fileJson["Entities"].is_array()) {
		return false;
	}
	PrefabReferenceRemapper::RepairPrefabFileScriptRefs(fileJson, prefabAsset);
	PrefabReferenceRemapper::NormalizePrefabFileJointAttachments(fileJson);

	// ファイルからプレファブ読み込み
	PrefabHeader header{};
	if (fileJson.contains("Header") && fileJson["Header"].is_object()) {

		FromJson(fileJson["Header"], header);
	} else {

		header.guid = prefabAsset;
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

	std::unordered_map<UUID, UUID> prefabLocalToSceneLocal;
	std::vector<std::pair<Entity, const nlohmann::json*>> pendingLoads;
	pendingLoads.reserve(fileJson["Entities"].size());

	//============================================================================
	//	エンティティを作成し、ローカルからシーンのIDへのマップを構築する
	//============================================================================
	for (const auto& entityJson : fileJson["Entities"]) {

		// プレファブファイル内のローカルIDを読み取る
		UUID prefabLocalFileID = ReadEntityLocalFileID(entityJson);

		// エンティティを作成しルートかつ予約済みEntityがあればそれをルートとしてmaterializeしIDを保つ
		Entity entity;
		if (world.IsAlive(desc.reservedRoot) && prefabLocalFileID == header.rootLocalFileID) {
			entity = desc.reservedRoot;
		} else {
			entity = world.CreateEntity();
		}
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
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
		SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID,
			outResult.prefabInstanceID, prefabLocalFileID == header.rootLocalFileID);

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
		auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);
		MeshSubMeshAuthoring::SyncComponent(&database, meshRenderer, true);
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
		const std::string baseName = namePath.string();
		if (!baseName.empty()) {

			// 同名インスタンスがあれば name_N へずらす、生成中のルート自身は判定から外す
			world.GetComponent<NameComponent>(outResult.root).name =
				SceneAuthoring::MakeUniqueEntityName(world, baseName, outResult.root);
		}
	}

	// 複数ルートだった旧プレファブを生成してもバラけず単一ルートにまとまる
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
	return true;
}

bool Engine::PrefabSystem::InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem,
	ECSWorld& world, const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc) const {

	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);
	return InstantiatePrefab(database, hierarchySystem, world, prefabAsset, outResult, desc);
}

Engine::UUID Engine::PrefabSystem::AllocateUniqueLocalFileID(ECSWorld& world) const {

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

std::string Engine::PrefabSystem::BuildDefaultPrefabName(ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath) const {

	// ルートエンティティの名前をベースにする
	if (world.IsAlive(root) && world.HasComponent<NameComponent>(root)) {
		const std::string& name = world.GetComponent<NameComponent>(root).name;
		if (!name.empty()) {
			return name;
		}
	}
	// ルートエンティティの名前が空の場合は、ファイル名をベースにする
	std::filesystem::path path = std::filesystem::path(prefabAssetPath).stem();
	if (path.extension() == ".prefab") {
		path = path.stem();
	}

	return path.string();
}
