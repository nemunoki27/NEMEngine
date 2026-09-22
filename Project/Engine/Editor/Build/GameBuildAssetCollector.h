#pragma once

//============================================================================
//	include
//============================================================================
#include "GameBuildTypes.h"
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <deque>
#include <map>
#include <unordered_set>
#include <vector>

namespace Engine {

	class AssetDatabase;
	struct AssetMeta;

	//============================================================================
	//	GameBuildAssetCollector class
	//	製品へ配置する依存ファイルと診断を収集する
	//============================================================================
	class GameBuildAssetCollector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 例外を診断へ変換して依存一覧を収集する
		static bool CollectFiles(AssetID startupScene, const AssetDatabase& database,
			std::vector<GameBuildFileEntry>& outFiles, std::string& error, SceneAssetStorage* sceneStorage);

		explicit GameBuildAssetCollector(const Engine::AssetDatabase& database, Engine::SceneAssetStorage* sceneStorage);
		bool Collect(Engine::AssetID startupScene, std::vector<GameBuildFileEntry>& outFiles, std::string& outError);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const Engine::AssetDatabase& database_;
		Engine::SceneAssetStorage standaloneStorage_;
		Engine::SceneAssetStorage* sceneStorage_ = nullptr;
		std::deque<Engine::AssetID> assetQueue_;
		std::unordered_set<Engine::AssetID> queuedAssets_;
		std::unordered_set<Engine::AssetID> requiredAssets_;
		std::unordered_set<std::string> scannedShaderFiles_;
		std::map<std::string, std::filesystem::path> files_;
		std::vector<std::string> errors_;

		//--------- functions ----------------------------------------------------

		// 参照先を処理待ちへ追加
		void AddAsset(Engine::AssetID assetID, bool required = false);
		// 配置先とファイルを検証
		void AddFile(const std::filesystem::path& source, const std::string& destination, bool required = false);
		// 製品内の配置先を解決
		std::string ToBuildDestination(const std::string& assetPath) const;
		// Asset本体とmetaを配置一覧へ追加
		void AddAssetFile(const Engine::AssetMeta& meta);
		// 論理pathの本体とmetaを収集
		void AddLogicalFile(const std::string& assetPath);
		// ゲーム内の配布対象を列挙
		void AddAllGameAssets();
		// Package内の配布対象を列挙
		void AddPackageFiles();
		// 参照待ちのAssetを順に解析
		void ProcessAssets();
		// 形式ごとの参照解析へ振り分ける
		void InspectFile(const Engine::AssetMeta& meta, const std::filesystem::path& source);
		// Sceneの分割文書を収集
		void CollectExternalActors(const Engine::AssetMeta& sceneMeta,
			const std::filesystem::path& scenePath,
			const nlohmann::json& sceneData);
		// 保存データの参照を収集
		void InspectJson(const nlohmann::json& node);
		// Shaderのincludeを再帰収集
		void CollectShaderIncludes(const std::filesystem::path& shaderPath);
		// モデル形式ごとの付随ファイルを収集
		void CollectModelSidecars(const std::filesystem::path& modelPath);
		// モデル文書の外部画像とBufferを収集
		void CollectModelUris(const nlohmann::json& node, const std::filesystem::path& modelDirectory);
		// OBJが参照するMaterialを収集
		void CollectObjSidecars(const std::filesystem::path& modelPath);
		// MTLが参照するTextureを収集
		void CollectMtlTextures(const std::filesystem::path& materialPath);
		// モデルの付随ファイルを配置一覧へ追加
		void AddModelSidecar(const std::filesystem::path& source);
		// 実行時必須Assetと設定を収集
		void AddFixedRuntimeFiles();
		// 既定Materialを必須参照へ追加
		void AddDefaultMaterialAssets();
	};
}
