#include "PrefabSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/PrefabLinkComponent.h>
#include <Engine/Core/Scene/SceneAuthoring.h>
#include <Engine/Utility/Json/JsonAdapter.h>

//============================================================================
//	PrefabSystem classMethods
//============================================================================

namespace {

	// プレファブファイル内でエンティティを識別するためのIDを読み取る
	static Engine::UUID ReadEntityLocalFileID(const nlohmann::json& entityJson) {

		std::string localFileID = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
		return localFileID.empty() ? Engine::UUID::New() : Engine::FromString16Hex(localFileID);
	}
}

bool Engine::PrefabSystem::SavePrefab(AssetDatabase& database, ECSWorld& world,
	const Entity& root, const std::string& prefabAssetPath) const {

	// ルートエンティティが存在するか
	if (!world.IsAlive(root)) {
		return false;
	}

	// プレファブアセットを登録
	const AssetID prefabAsset = database.ImportOrGet(prefabAssetPath, AssetType::Prefab);

	// サブツリー収集
	std::vector<Entity> subtree = CollectSubtree(world, root);
	if (subtree.empty()) {
		return false;
	}

	// ルート情報をデフォルト構築
	SceneAuthoring::EnsureGameObjectDefaults(world, root);
	const auto& rootSceneObject = world.GetComponent<SceneObjectComponent>(root);

	// プレファブファイルの構築
	PrefabHeader header{};
	header.guid = prefabAsset;
	header.name = BuildDefaultPrefabName(world, root, prefabAssetPath);
	header.rootLocalFileID = rootSceneObject.localFileID;
	header.version = 1;

	nlohmann::json fileJson = nlohmann::json::object();

	fileJson["Header"] = ToJson(header);
	fileJson["Entities"] = nlohmann::json::array();

	// エンティティごとにコンポーネントをシリアライズしてファイルのnlohmann::jsonに追加
	for (const Entity& entity : subtree) {

		if (!world.IsAlive(entity)) {
			continue;
		}

		// デフォルトのコンポーネントを構築
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);

		// エンティティ
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

		// コンポーネント
		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);

		// プレファブ自体の中に、別プレファブ由来情報は持ち込まない
		components.erase("PrefabLink");

		// コンポーネントをエンティティに追加
		entityJson["Components"] = std::move(components);
		fileJson["Entities"].push_back(std::move(entityJson));
	}

	// ファイルに保存
	JsonAdapter::Save(prefabAssetPath, fileJson);
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

	// ファイルからプレファブ読み込み
	PrefabHeader header{};
	if (fileJson.contains("Header") && fileJson["Header"].is_object()) {

		FromJson(fileJson["Header"], header);
	} else {

		header.guid = prefabAsset;
	}

	// シーン内で一意なプレファブインスタンスIDを生成
	outResult.prefabInstanceID = UUID::New();

	std::unordered_map<UUID, UUID> prefabLocalToSceneLocal;
	std::vector<std::pair<Entity, const nlohmann::json*>> pendingLoads;
	pendingLoads.reserve(fileJson["Entities"].size());

	//============================================================================
	//	エンティティを作成し、ローカルからシーンのIDへのマップを構築する
	//============================================================================
	for (const auto& entityJson : fileJson["Entities"]) {

		// プレファブファイル内のローカルIDを読み取る
		UUID prefabLocalFileID = ReadEntityLocalFileID(entityJson);

		// エンティティを作成
		Entity entity = world.CreateEntity();
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		UUID newSceneLocalFileID = AllocateUniqueLocalFileID(world);
		// シーンオブジェクト初期化
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = newSceneLocalFileID;
			sceneObject.sourceAsset = prefabAsset;
			sceneObject.sceneInstanceID = desc.ownerSceneInstanceID;
		}
		// プレファブリンク初期化
		{
			auto& prefabLink = world.AddComponent<PrefabLinkComponent>(entity);
			prefabLink.prefabAsset = prefabAsset;
			prefabLink.prefabLocalFileID = prefabLocalFileID;
			prefabLink.prefabInstanceID = outResult.prefabInstanceID;
			prefabLink.isPrefabRoot = (prefabLocalFileID == header.rootLocalFileID);
		}

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
			const nlohmann::json& data = it.value();
			if (typeName == "SceneObject" || typeName == "PrefabLink") {
				continue;
			}
			world.AddComponentFromJson(entity, typeName, data);
		}
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
		hierarchy.parentLocalFileID = (it != prefabLocalToSceneLocal.end()) ? it->second : UUID{};
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

std::vector<Engine::Entity> Engine::PrefabSystem::CollectSubtree(ECSWorld& world, const Entity& root) const {

	// ルートが存在しない場合は空を返す
	std::vector<Entity> result;
	if (!world.IsAlive(root)) {
		return result;
	}

	// 深さ優先探索でサブツリーを収集する
	std::stack<Entity> stack;
	stack.push(root);
	while (!stack.empty()) {

		Entity entity = stack.top();
		stack.pop();

		// エンティティが存在しない場合はスキップ
		if (!world.IsAlive(entity)) {
			continue;
		}

		result.emplace_back(entity);
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			continue;
		}

		// 子を走査してスタックに追加
		Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
		while (child.IsValid()) {

			stack.push(child);
			child = world.GetComponent<HierarchyComponent>(child).nextSibling;
		}
	}
	return result;
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
	std::filesystem::path path(prefabAssetPath);
	return path.stem().string();
}