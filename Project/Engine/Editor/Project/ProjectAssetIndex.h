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

		bool Rebuild(const AssetDatabase& database);

		//--------- accessor -----------------------------------------------------

		const ProjectDirectoryNode& GetRoot() const { return root_; }
		const ProjectDirectoryNode* FindDirectory(const std::string& virtualPath) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクトのルートディレクトリノード
		ProjectDirectoryNode root_{};

		//--------- functions ----------------------------------------------------

		static bool ShouldHideInBrowser(const std::filesystem::path& fullPath);
		static std::vector<std::string> CollectSidecars(const std::filesystem::path& fullPath);
		static std::string MakeDisplayName(const std::filesystem::path& fullPath);

		ProjectDirectoryNode* EnsureDirectory(const std::filesystem::path& relativeDirectory);
		static void SortRecursive(ProjectDirectoryNode& node);
		static const ProjectDirectoryNode* FindRecursive(const ProjectDirectoryNode& node, const std::string& virtualPath);
	};
} // Engine