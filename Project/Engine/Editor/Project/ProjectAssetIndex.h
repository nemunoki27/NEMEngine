#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>

// c++
#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace Engine {

	//============================================================================
	//	ProjectAssetIndex structures
	//============================================================================

	// プロジェクト内のアセットのエントリー
	struct ProjectAssetEntry {

		// アセットの識別ID
		AssetID assetID{};
		AssetType type = AssetType::Unknown;

		// Assets/...
		std::string assetPath;
		// 実ファイル名
		std::string fileName;
		// パネル表示名
		std::string displayName;
		// サイドカーファイルのパス（Assets/...）
		std::vector<std::string> sidecarFiles;
	};
	// プロジェクト内のディレクトリノード
	struct ProjectDirectoryNode {

		// ディレクトリ名
		std::string name;
		// ディレクトリの仮想パス（Assets/...）
		std::string virtualPath;
		// 子ディレクトリ
		std::vector<std::unique_ptr<ProjectDirectoryNode>> children;
		// ディレクトリ内のアセット
		std::vector<ProjectAssetEntry> assets;
	};

	//============================================================================
	//	ProjectAssetSource enum class
	//============================================================================
	enum class ProjectAssetSource :
		uint8_t {

		Engine,
		Game,
	};

	//============================================================================
	//	ProjectAssetIndex class
	//	プロジェクト内のアセットのインデックスを管理するクラス
	//============================================================================
	class ProjectAssetIndex {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetIndex() = default;
		~ProjectAssetIndex() = default;

		bool Rebuild(const AssetDatabase& database, ProjectAssetSource source);

		//--------- accessor -----------------------------------------------------

		const ProjectDirectoryNode& GetRoot() const { return root_; }
		const ProjectDirectoryNode* FindDirectory(const std::string& virtualPath) const;
		const ProjectAssetEntry* FindAssetByPath(const std::string& assetPath) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクトのルートディレクトリノード
		ProjectDirectoryNode root_{};

		//--------- functions ----------------------------------------------------

		// ブラウザ上に表示しないファイルかを判定する
		static bool ShouldHideInBrowser(const std::filesystem::path& fullPath);
		// 表示アセットに紐付く付属ファイルを集める
		static std::vector<std::string> CollectSidecars(const std::filesystem::path& fullPath);
		// アセット一覧で使う表示名を作る
		static std::string MakeDisplayName(const std::filesystem::path& fullPath);

		// 必要なディレクトリノードを作成して返す
		ProjectDirectoryNode* EnsureDirectory(const std::filesystem::path& relativeDirectory);
		// ディレクトリとアセットを表示順に並び替える
		static void SortRecursive(ProjectDirectoryNode& node);
		// 仮想パスに一致するディレクトリを再帰的に探す
		static const ProjectDirectoryNode* FindRecursive(const ProjectDirectoryNode& node, const std::string& virtualPath);
		// アセットパスに一致するアセットを再帰的に探す
		static const ProjectAssetEntry* FindAssetRecursive(const ProjectDirectoryNode& node, const std::string& assetPath);
	};
} // Engine
