#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/Header/SceneHeader.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Utility/Json/JsonAdapter.h>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	SceneSystem class
	//	ECSWorldの内容をファイルへ保存/ファイルから読み込むクラス
	//============================================================================
	class SceneSystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneSystem() = default;
		~SceneSystem() = default;

		// ファイルからワールドをロード
		bool LoadScene(const std::string& scenePath, ECSWorld& world, AssetDatabase* assetDatabase, AssetID sourceAsset = AssetID{},
			UUID sceneInstanceID = UUID{}, SceneHeader* outHeader = nullptr, std::vector<Entity>* outCreatedEntities = nullptr) const;
		// ワールドをファイルへセーブ
		bool SaveScene(const std::string& scenePath, ECSWorld& world,
			const SceneHeader& header, const std::vector<Entity>* entitiesSubset = nullptr) const;

		// nlohmann::jsonスナップショット
		nlohmann::json SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset = nullptr) const;
		// nlohmann::jsonからワールドを作成
		bool LoadFromJson(const nlohmann::json& root, ECSWorld& world, AssetDatabase* assetDatabase = nullptr,
			AssetID sourceAsset = AssetID{}, UUID sceneInstanceID = UUID{},
			std::vector<Entity>* outCreatedEntities = nullptr) const;
	};
} // Engine