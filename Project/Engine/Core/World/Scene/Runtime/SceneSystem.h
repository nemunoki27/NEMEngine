#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	SceneSystem class
	//	ECSWorldの内容をファイルへ保存/ファイルから読み込むクラス
	//============================================================================
	class SceneSystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SceneSystem() = default;
		~SceneSystem() = default;

		// ファイルからワールドをロード
		bool LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase, AssetID sourceAsset = AssetID{},
			UUID sceneInstanceID = UUID{}, SceneHeader* outHeader = nullptr, std::vector<Entity>* outCreatedEntities = nullptr) const;
		// ワールドをファイルへセーブ、databaseがあればプレファブインスタンスを薄い差分形式で保存する
		bool SaveScene(const std::filesystem::path& scenePath, ECSWorld& world,
			const SceneHeader& header, const std::vector<Entity>* entitiesSubset = nullptr,
			AssetDatabase* database = nullptr) const;

		// nlohmann::jsonスナップショット
		nlohmann::json SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset = nullptr) const;
		// nlohmann::jsonからワールドを作成
		bool LoadFromJson(const nlohmann::json& root, ECSWorld& world, AssetDatabase* assetDatabase = nullptr,
			AssetID sourceAsset = AssetID{}, UUID sceneInstanceID = UUID{},
			std::vector<Entity>* outCreatedEntities = nullptr) const;
	};
} // Engine
