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
#include <functional>
#include <mutex>
#include <unordered_map>

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

		// Sceneと子Sceneの文書をWorldに触れず準備する
		bool PrepareSceneDocuments(AssetID rootAsset, const std::unordered_map<AssetID, std::filesystem::path>& paths,
			const std::function<bool(const nlohmann::json&)>& prepareAssets = {}) const;

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

		//--------- structure ----------------------------------------------------

		struct PreparedDocument {

			nlohmann::json root;
			std::string revision;
		};
		struct CachedDocument {

			std::shared_ptr<const PreparedDocument> document;
			uint64_t lastAccess = 0;
		};
		struct DocumentCache {

			std::mutex mutex;
			std::unordered_map<std::string, CachedDocument> entries;
			uint64_t accessSerial = 0;
		};

		//--------- variables ----------------------------------------------------

		// プロジェクト内の読込・保存・修復で共有する状態
		std::shared_ptr<SceneAssetStorage> storage_;
		std::shared_ptr<DocumentCache> documentCache_ = std::make_shared<DocumentCache>();

		//--------- functions ----------------------------------------------------

		// 保存世代が一致する文書だけを再利用する
		std::shared_ptr<const PreparedDocument> ReadSceneDocument(const std::filesystem::path& path, AssetID asset) const;
	};
} // Engine
