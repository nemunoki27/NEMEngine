#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <unordered_map>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	AssetDatabase struct
	//============================================================================
	// アセットのメタデータ
	struct AssetMeta {

		// 識別ID
		AssetID guid{};
		AssetType type = AssetType::Unknown;
		std::string importer;
		uint32_t importerVersion = 1;
		nlohmann::json importerSettings = nlohmann::json::object();

		// アセットのファイルパス
		std::string assetPath;

		// アセットの依存先
		std::vector<AssetID> dependencies;
		uint64_t importHash = 0;
	};

	// データベース構築時に検出した問題の種別
	enum class AssetDatabaseIssueType {

		CorruptMeta,            // .metaが壊れている
		DuplicateGuid,          // 同一GUIDが複数アセットに存在
		DuplicatePath,          // 検索キーが衝突
		OrphanMeta,             // 実体のない.meta
		MissingReference,       // 参照先アセットが存在しない
		ReferenceTypeMismatch,  // 参照先の型が期待と異なる
		UnknownAssetType,       // 種別を判定できない
	};
	// 構築時診断の1件分
	struct AssetDatabaseIssue {

		AssetDatabaseIssueType type;
		AssetID assetID{};
		AssetID referencedAssetID{};
		AssetType expectedType = AssetType::Unknown;
		AssetType actualType = AssetType::Unknown;
		std::string assetPath;
		std::string relatedPath;
		std::string detail;
	};

	//============================================================================
	//	AssetDatabase class
	//	アセットファイルのメタデータを管理するクラス
	//============================================================================
	class AssetDatabase {
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

		// アセットをインポートするか、すでに存在する場合は識別IDを返す
		AssetID ImportOrGet(const std::string& assetPath, AssetType guessedType);
		// 指定アセットの依存関係と逆引き参照を現在のファイル内容で更新する
		void RefreshDependencies(AssetID id);
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
		const std::unordered_map<AssetID, AssetMeta>& GetAssets() const { return guidToMeta_; }

		// アセット集合の構造リビジョンを取得、RebuildMetaのたびに増えるので差分監視に使う
		uint64_t GetStructureRevision() const { return structureRevision_; }

		// ファイルパスのルートを取得
		const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }
		const std::filesystem::path& GetAssetsRoot() const { return assetsRoot_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

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

		//--------- functions ----------------------------------------------------

		// 検索用のパスキーでWindowsの大文字小文字差を吸収する、保存表記とは別
		static std::string NormalizeLookupKey(const std::string& assetPath);
		// アセットファイルのフルパスからメタファイルのフルパスを取得
		static std::filesystem::path MetaPathOf(const std::filesystem::path& assetFullPath);

		// ファイル走査とUID索引の構築で重複や破損や孤立を検出する
		void RebuildIndex(const std::vector<std::filesystem::path>& scanRoots);
		// 索引構築後に依存関係・逆引き参照・参照診断を構築する
		void RebuildDependencies();
		// font.jsonのatlasTexture参照を隣接アトラス画像の現在GUIDへ揃えて書き戻す
		void ReconcileFontAtlasReferences();
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

