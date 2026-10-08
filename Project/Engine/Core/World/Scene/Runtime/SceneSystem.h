#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Scene/Serialization/SceneSerializationTypes.h>
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

		SceneSystem();
		explicit SceneSystem(std::shared_ptr<SceneAssetStorage> storage);
		~SceneSystem() = default;

		// ファイルからワールドをロード
		bool LoadScene(const std::filesystem::path& scenePath, ECSWorld& world, AssetDatabase* assetDatabase, AssetID sourceAsset = AssetID{},
			UUID sceneInstanceID = UUID{}, SceneHeader* outHeader = nullptr, std::vector<Entity>* outCreatedEntities = nullptr) const;
		// ワールドをファイルへセーブしプレファブインスタンスを薄い差分形式で保存する
		bool SaveScene(const std::filesystem::path& scenePath, ECSWorld& world,
			const SceneHeader& header, AssetDatabase& database,
			const std::vector<Entity>* entitiesSubset = nullptr) const;
		// ECSWorldから保存データを取得し、以降のファイル書き込みをワーカーへ渡せる状態にする
		bool CaptureSaveSnapshot(const std::filesystem::path& scenePath,
			ECSWorld& world, const SceneHeader& header,
			AssetDatabase& database, SceneSaveSnapshot& outSnapshot,
			const std::vector<Entity>* entitiesSubset = nullptr) const;
		// 確定済みスナップショットをプロジェクト指定の保存形式で書き込む
		static bool WriteSaveSnapshot(SceneSaveSnapshot snapshot);
		// 保存済みシーンを新しいGUIDで複製し失敗時は作成分だけを取り消す
		static bool CopySceneAssets(const std::vector<SceneAssetCopy>& copies, std::string& error,
			std::shared_ptr<SceneAssetStorage> storage = {});

		// nlohmann::jsonスナップショット
		nlohmann::json SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset = nullptr) const;
		// nlohmann::jsonからワールドを作成
		bool LoadFromJson(const nlohmann::json& root, ECSWorld& world, AssetDatabase* assetDatabase = nullptr,
			AssetID sourceAsset = AssetID{}, UUID sceneInstanceID = UUID{},
			std::vector<Entity>* outCreatedEntities = nullptr) const;

		//--------- accessor -----------------------------------------------------

		const std::shared_ptr<SceneAssetStorage>& GetStorage() const { return storage_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクト内の読込・保存・修復で共有する状態
		std::shared_ptr<SceneAssetStorage> storage_;
	};
} // Engine
