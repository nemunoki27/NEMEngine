#pragma once

//============================================================================
//	include
//============================================================================
#include "AssetMetadata.h"

// c++
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	AssetDatabase class
	//	アセットファイルのメタデータを管理するクラス
	//============================================================================
	class AssetDatabase {
		friend class AssetDocumentPublication;
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AssetDatabase() = default;
		~AssetDatabase() = default;

		// 初期化
		bool Init();
		// メタデータをすべて取得
		bool RebuildMeta();
		// 指定した走査範囲から索引全体を作り直す
		bool RebuildMeta(const std::vector<std::filesystem::path>& scanRoots);
		// 検出済みの孤立metaを元Assetの不在を確認して削除する
		bool DeleteOrphanMeta(const std::filesystem::path& metaPath);
		// 欠損したFont参照を指定したAtlasへ修復する
		bool RepairFontAtlas(AssetID fontID, AssetID atlasID, std::string* diagnostic = nullptr);

		// アセットをインポートするか、すでに存在する場合は識別IDを返す
		AssetID ImportOrGet(const std::string& assetPath, AssetType guessedType);
		// 指定アセットの依存関係と逆引き参照を現在のファイル内容で更新する
		bool RefreshDependencies(AssetID id);
		// 保存完了を通知し、同じ更新時刻でも読込結果を失効させる
		void NotifyContentChanged(AssetID id);
		// Importer設定をメモリと.metaへ反映する
		bool UpdateImporterSettings(AssetID id, const nlohmann::json& settings,
			uint32_t importerVersion);
		// 索引を変更せずメタデータファイルを読み書きする
		static bool ReadMetaFile(const std::filesystem::path& metaFullPath, AssetMeta& out);
		static bool WriteMetaFile(const std::filesystem::path& metaFullPath, const AssetMeta& meta);

		//--------- accessor -----------------------------------------------------

		// 識別IDからメタデータを取得
		const AssetMeta* Find(AssetID id) const;
		// パスからメタデータIDを取得
		const AssetMeta* FindByPath(const std::string& assetPath) const;
		// GUIDからアセットのファイルパスを取得
		std::filesystem::path ResolveFullPath(AssetID id) const;
		// 論理アセットパスから実ファイルパスを取得
		std::filesystem::path ResolveAssetPath(const std::string& assetPath) const;

		// 依存関係・逆引き参照・診断の取得(該当なしは共通の空vectorを返す)
		const std::vector<AssetID>& FindDependencies(AssetID id) const;
		const std::vector<AssetID>& FindReferencers(AssetID id) const;
		// 循環参照を除外しながら全ての間接参照元を取得
		std::vector<AssetID> FindReferencersRecursive(AssetID id) const;
		bool HasReferencers(AssetID id) const;
		const std::vector<AssetDatabaseIssue>& GetIssues() const { return issues_; }
		const std::string& GetLastRebuildError() const { return lastRebuildError_; }
		const std::unordered_map<AssetID, AssetMeta>& GetAssets() const { return guidToMeta_; }

		// アセット集合の構造リビジョンを取得、RebuildMetaのたびに増えるので差分監視に使う
		uint64_t GetStructureRevision() const { return structureRevision_; }
		// この索引で通知された内容の更新番号を取得する
		uint64_t GetContentRevision(AssetID id) const;
		// 索引全体で通知された内容の更新番号を取得する
		uint64_t GetContentRevision() const { return contentRevision_; }
		// 索引の複製や破棄を派生データの所有元へ通知する
		std::weak_ptr<const uint8_t> GetCacheLifetime() const { return cacheLifetime_.identity; }

		// ファイルパスのルートを取得
		const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }
		const std::filesystem::path& GetAssetsRoot() const { return assetsRoot_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct CacheLifetime {

			std::shared_ptr<const uint8_t> identity = std::make_shared<const uint8_t>(0);

			CacheLifetime() = default;
			CacheLifetime(const CacheLifetime&);
			CacheLifetime& operator=(const CacheLifetime&);
		};

		//--------- variables ----------------------------------------------------

		CacheLifetime cacheLifetime_;

		// ファイルのディレクトリパス
		std::filesystem::path projectRoot_;
		std::filesystem::path assetsRoot_;

		// メタデータのマップ
		std::unordered_map<AssetID, AssetMeta> guidToMeta_;
		// 検索用に正規化したパスキーから識別IDへのマップ
		std::unordered_map<std::string, AssetID> pathToGuid_;
		// 逆引き参照(依存される側ID ->参照しているアセットID群)
		std::unordered_map<AssetID, std::vector<AssetID>> referencersByGuid_;
		// 構築時に検出した問題一覧
		std::vector<AssetDatabaseIssue> issues_;
		// アセット集合の構造リビジョン、RebuildMetaのたびに増える
		uint64_t structureRevision_ = 0;
		// 個別の更新番号と索引全体の更新番号
		uint64_t contentRevision_ = 0;
		std::unordered_map<AssetID, uint64_t> contentRevisions_;
		// 直近の走査失敗と再検査する範囲
		std::string lastRebuildError_;
		std::vector<std::filesystem::path> scanRoots_;
		bool buildingIndex_ = false;

		//--------- functions ----------------------------------------------------

		// 検索用のパスキーでWindowsの大文字小文字差を吸収する、保存表記とは別
		static std::string NormalizeLookupKey(const std::string& assetPath);
		// アセットファイルのフルパスからメタファイルのフルパスを取得
		static std::filesystem::path MetaPathOf(const std::filesystem::path& assetFullPath);

		// ファイル走査とUID索引の構築で重複や破損や孤立を検出する
		void RebuildIndex(const std::vector<std::filesystem::path>& scanRoots);
		// 索引構築後に依存関係・逆引き参照・参照診断を構築する
		void RebuildDependencies();
		// 1つのアセットファイルを索引へ登録する
		AssetID RegisterAssetFile(const std::filesystem::path& assetFullPath);
		// 指定アセットの依存先を抽出する
		std::vector<AssetID> ExtractDependencies(const AssetMeta& meta);
		// 孤立した.metaを検出する
		void DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots);
		// 診断を追加する
		void AddIssue(AssetDatabaseIssue&& issue);
	};
} // Engine

