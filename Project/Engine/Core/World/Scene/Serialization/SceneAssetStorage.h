#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <filesystem>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	class AssetDatabase;
	struct SceneSaveSnapshot;
	struct JsonFileChange;
	using SceneStorageChange = JsonFileChange;

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
		std::vector<SceneStorageIssue> Validate(const std::filesystem::path& scenePath, AssetID sceneAsset);
		// 全シーンと所有元のないActorフォルダーを検証する
		std::vector<SceneStorageIssue> Inspect(const AssetDatabase& database);
		// 読み込み時のディスク状態を保持する
		void TrackLoaded(const std::filesystem::path& scenePath, AssetID sceneAsset);
		// エディターが使用中のシーンを削除・修復から保護する
		void SetProtectedScenes(const std::vector<AssetID>& sceneAssets);
		// 変更前のファイルを退避して保存する
		bool Save(SceneSaveSnapshot snapshot, std::string& error);
		// シーンまたはフォルダーと所有Actorを退避して削除する
		bool Delete(const std::filesystem::path& path, const AssetDatabase& database, std::string& error);
		// 元のActorファイルを指定して欠損を復元する
		bool RestoreActor(const std::filesystem::path& scenePath, UUID actorID,
			const std::filesystem::path& source, std::string& error);
		// 参照の残った欠損Actorを明示的に削除する
		bool RemoveMissingActor(const std::filesystem::path& scenePath, UUID actorID, std::string& error);
		// 削除確定が変更するファイルを保存前に確認する
		bool PreviewMissingActorRemoval(const std::filesystem::path& scenePath, UUID actorID,
			std::vector<std::filesystem::path>& affectedFiles, std::string& error);
		// 退避された操作記録を取得する
		std::vector<std::filesystem::path> GetRecoveries(bool unfinishedOnly = false);
		// 退避データから操作前の状態へ戻す
		bool Recover(const std::filesystem::path& directory, std::string& error);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクトの編集セッションで共有する保存状態
		std::recursive_mutex storageMutex_;
		std::unordered_map<std::string, std::string> loadedRevisions_;
		std::unordered_map<std::string, AssetID> loadedAssets_;
		std::unordered_set<AssetID> protectedScenes_;

		//--------- functions ----------------------------------------------------

		// 操作中の失敗では自身の保護判定を除いて復旧する
		bool RecoverInternal(const std::filesystem::path& directory, std::string& error, bool rollingBack);
		// Sceneの使用状態を保護してファイル変更を確定する
		bool Commit(const std::vector<SceneStorageChange>& changes, const std::string& label, std::string& error);
		// 編集中のSceneへの破壊的変更を拒否する
		void RequireClosed(AssetID id);
		// 欠損Actorの参照変更を検証して反映する
		bool UpdateMissingActor(const std::filesystem::path& scenePath, UUID actorID,
			std::string& error, std::vector<std::filesystem::path>* preview);
	};
}
