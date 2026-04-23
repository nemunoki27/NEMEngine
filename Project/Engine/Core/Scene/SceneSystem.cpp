#include "SceneSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/SceneAuthoring.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Core/Graphics/Mesh/MeshSubMeshAuthoring.h>

//============================================================================
//	SceneSystem classMethods
//============================================================================

bool Engine::SceneSystem::LoadScene(const std::string& scenePath, ECSWorld& world, AssetDatabase* assetDatabase,
	AssetID sourceAsset, UUID sceneInstanceID, SceneHeader* outHeader, std::vector<Entity>* outCreatedEntities) const {

	// ファイルからnlohmann::jsonをロード
	nlohmann::json root = JsonAdapter::Load(scenePath, true);
	if (outHeader) {
		if (root.contains("Header") && root["Header"].is_object()) {

			FromJson(root["Header"], *outHeader, assetDatabase);
		} else {

			outHeader->guid = UUID::New();
			outHeader->name = "UntitledScene";
		}
	}
	return LoadFromJson(root, world, assetDatabase, sourceAsset, sceneInstanceID, outCreatedEntities);
}

bool Engine::SceneSystem::SaveScene(const std::string& scenePath, ECSWorld& world,
	const SceneHeader& header, const std::vector<Entity>* entitiesSubset) const {

	nlohmann::json root = nlohmann::json::object();

	root["Header"] = ToJson(header);
	root["Entities"] = SerializeEntities(world, entitiesSubset);

	JsonAdapter::Save(scenePath, root);
	return true;
}

nlohmann::json Engine::SceneSystem::SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset) const {

	nlohmann::json array = nlohmann::json::array();

	// subsetが指定されている場合はそのエンティティのみをシリアライズする。subset内のエンティティが存在しない場合は無視する
	if (subset) {
		for (const Entity& entity : *subset) {

			// エンティティが存在しない場合は無視
			if (!world.IsAlive(entity)) {
				continue;
			}

			// デフォルトコンポーネントの追加
			SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);

			// エンティティのシリアライズ
			nlohmann::json entityJson = nlohmann::json::object();
			entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

			nlohmann::json components = nlohmann::json::object();
			world.SerializeEntityComponents(entity, components);
			entityJson["Components"] = components;

			// エンティティのnlohmann::jsonを配列に追加
			array.push_back(entityJson);
		}
		return array;
	}

	// subsetが指定されていない場合は全てのエンティティをシリアライズする
	world.ForEachAliveEntity([&](const Entity& entity) {

		// デフォルトコンポーネントの追加
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);

		// エンティティのシリアライズ
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		entityJson["Components"] = components;

		// エンティティのnlohmann::jsonを配列に追加
		array.push_back(entityJson);
		});
	return array;
}

bool Engine::SceneSystem::LoadFromJson(const nlohmann::json& root, ECSWorld& world,
	AssetDatabase* assetDatabase, AssetID sourceAsset, UUID sceneInstanceID,
	std::vector<Entity>* outCreatedEntities) const {

	// ルートがオブジェクトでなければ失敗
	if (!root.is_object()) {
		return false;
	}

	// Entitiesが無いなら「空シーン」として成功扱い
	if (!root.contains("Entities")) {
		return true;
	}

	const nlohmann::json& entitiesNode = root["Entities"];

	// {}も空シーンとして受け入れる
	if (entitiesNode.is_object() && entitiesNode.empty()) {
		return true;
	}

	// []以外は不正
	if (!entitiesNode.is_array()) {
		return false;
	}

	// "Entities"配列をループしてエンティティを作成し、コンポーネントを追加する
	for (const auto& entityJson : entitiesNode) {

		// "LocalFileID"が存在しない場合は"UUID"を探す。どちらも存在しない場合はSceneObject側の値を使う
		std::string localFileIDStr = entityJson.value("LocalFileID", entityJson.value("UUID", ""));
		UUID localFileID = localFileIDStr.empty() ? UUID{} : FromString16Hex(localFileIDStr);

		// エンティティの作成
		Entity entity = world.CreateEntity();
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);

		// 作成されたエンティティを出力する
		if (outCreatedEntities) {

			outCreatedEntities->emplace_back(entity);
		}

		// "Components"オブジェクトが存在する場合はコンポーネントを追加する
		if (entityJson.contains("Components") && entityJson["Components"].is_object()) {

			const auto& components = entityJson["Components"];
			for (auto it = components.begin(); it != components.end(); ++it) {

				const std::string& typeName = it.key();
				const nlohmann::json& data = it.value();
				world.AddComponentFromJson(entity, typeName, data);
			}
		}
		// JSONからSceneObjectを読み直した後に、ランタイム所属情報を設定する
		SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		{
			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (localFileID) {

				sceneObject.localFileID = localFileID;
			} else if (!sceneObject.localFileID) {

				sceneObject.localFileID = UUID::New();
			}
			sceneObject.sourceAsset = sourceAsset;
			sceneObject.sceneInstanceID = sceneInstanceID;
		}
		// ロード直後に、ワールド実体のサブメッシュを正規化する
		if (assetDatabase && world.HasComponent<MeshRendererComponent>(entity)) {

			auto& meshRenderer = world.GetComponent<MeshRendererComponent>(entity);
			MeshSubMeshAuthoring::SyncComponent(assetDatabase, meshRenderer, true);
		}
	}
	return true;
}
