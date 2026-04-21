#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <unordered_map>
#include <filesystem>

namespace Engine {

	//============================================================================
	//	AssetDatabase struct
	//============================================================================

	// アセットのメタデータ
	struct AssetMeta {

		// 識別ID
		AssetID guid{};
		AssetType type = AssetType::Unknown;

		// アセットのファイルパス
		std::string assetPath;

		// アセットの依存先
		std::vector<AssetID> dependencies;
		uint64_t importHash = 0;
	};

	//============================================================================
	//	AssetDatabase class
	//	アセットファイルのメタデータを管理するクラス
	//============================================================================
	class AssetDatabase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetDatabase() = default;
		~AssetDatabase() = default;

		// 初期化
		bool Init();
		// メタデータをすべて取得
		bool RebuildMeta();

		// アセットをインポートするか、すでに存在する場合は識別IDを返す
		AssetID ImportOrGet(const std::string& assetPath, AssetType guessedType);

		//--------- accessor -----------------------------------------------------

		// 識別IDからメタデータを取得
		const AssetMeta* Find(AssetID id) const;
		// パスからメタデータIDを取得
		const AssetMeta* FindByPath(const std::string& assetPath) const;
		// GUIDからアセットのファイルパスを取得
		std::filesystem::path ResolveFullPath(AssetID id) const;
		// 論理アセットパスから実ファイルパスを取得
		std::filesystem::path ResolveAssetPath(const std::string& assetPath) const;

		// ファイルパスのルートを取得
		const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }
		const std::filesystem::path& GetAssetsRoot() const { return assetsRoot_; }
		const std::filesystem::path& GetLibraryRoot() const { return libraryRoot_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ファイルのディレクトリパス
		std::filesystem::path projectRoot_;
		std::filesystem::path assetsRoot_;
		std::filesystem::path libraryRoot_;

		// メタデータのマップ
		std::unordered_map<AssetID, AssetMeta> guidToMeta_;
		// 正規化されたファイルパスから識別IDへのマップ
		std::unordered_map<std::string, AssetID> pathToGuid_;

		//--------- functions ----------------------------------------------------

		// パスの正規化
		static std::string NormalizePath(const std::filesystem::path& path);
		// アセットファイルのフルパスからメタファイルのフルパスを取得
		static std::filesystem::path MetaPathOf(const std::filesystem::path& assetFullPath);

		// ファイルパスからアセットの種類を推測
		AssetType GuessTypeByPath(const std::filesystem::path& assetFullPath) const;

		// メタデータの読み書き
		bool TryLoadMeta(const std::filesystem::path& metaFullPath, AssetMeta& out) const;
		bool SaveMeta(const std::filesystem::path& metaFullPath, const AssetMeta& meta) const;
	};
} // Engine
