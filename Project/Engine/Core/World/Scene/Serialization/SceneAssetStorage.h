#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <filesystem>
#include <vector>

namespace Engine {

	class AssetDatabase;
	struct SceneSaveSnapshot;

	// シーン保存データの検証結果
	struct SceneStorageIssue {

		std::filesystem::path scenePath;
		std::filesystem::path actorPath;
		UUID actorID{};
		std::string detail;
		bool missing = false;
	};

	//============================================================================
	//	SceneAssetStorage class
	//	シーンと所有Actorの整合性を保って保存・削除・修復する
	//============================================================================
	class SceneAssetStorage {
	public:
		// Actorの所有シーンから配置先を解決する
		static std::filesystem::path ResolveActorRoot(const std::filesystem::path& scenePath, AssetID sceneAsset);
		// シーンが参照するActorを検証する
		static std::vector<SceneStorageIssue> Validate(const std::filesystem::path& scenePath, AssetID sceneAsset);
		// 全シーンと所有元のないActorフォルダーを検証する
		static std::vector<SceneStorageIssue> Inspect(const AssetDatabase& database);
		// 読み込み時のディスク状態を保持する
		static void TrackLoaded(const std::filesystem::path& scenePath, AssetID sceneAsset);
		// エディターが使用中のシーンを削除・修復から保護する
		static void SetProtectedScenes(const std::vector<AssetID>& sceneAssets);
		// 変更前のファイルを退避して保存する
		static bool Save(SceneSaveSnapshot snapshot, std::string& error);
		// シーンまたはフォルダーと所有Actorを退避して削除する
		static bool Delete(const std::filesystem::path& path, const AssetDatabase& database, std::string& error);
		// 元のActorファイルを指定して欠損を復元する
		static bool RestoreActor(const std::filesystem::path& scenePath, UUID actorID,
			const std::filesystem::path& source, std::string& error);
		// 参照の残った欠損Actorを明示的に削除する
		static bool RemoveMissingActor(const std::filesystem::path& scenePath, UUID actorID, std::string& error);
		// 削除確定が変更するファイルを保存前に確認する
		static bool PreviewMissingActorRemoval(const std::filesystem::path& scenePath, UUID actorID,
			std::vector<std::filesystem::path>& affectedFiles, std::string& error);
		// 退避された操作記録を取得する
		static std::vector<std::filesystem::path> GetRecoveries(bool unfinishedOnly = false);
		// 退避データから操作前の状態へ戻す
		static bool Recover(const std::filesystem::path& directory, std::string& error);
	private:
		static bool UpdateMissingActor(const std::filesystem::path& scenePath, UUID actorID,
			std::string& error, std::vector<std::filesystem::path>* preview);
	};
}
