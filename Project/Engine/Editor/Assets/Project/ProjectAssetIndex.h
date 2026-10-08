#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/ReadOnlyPointeeRange.h>

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

		// 論理Assetパス
		std::string assetPath;
		// 実ファイル名
		std::string fileName;
		// パネル表示名
		std::string displayName;
		// Assetと同じフォルダーにある所有ファイル名
		std::vector<std::string> sidecarFiles;
	};
	// プロジェクト内のディレクトリノード
	struct ProjectDirectoryNode {
		friend class ProjectAssetIndex;

		// ディレクトリ名
		std::string name;
		// ディレクトリの仮想パス
		std::string virtualPath;
		// ディレクトリ内のアセット
		std::vector<ProjectAssetEntry> assets;

		//--------- accessor -----------------------------------------------------

		// 子ディレクトリを読取専用で列挙する
		auto GetChildren() const { return ReadOnlyPointeeRange(children_); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 子ディレクトリの所有は索引へ限定する
		std::vector<std::unique_ptr<ProjectDirectoryNode>> children_;
	};

	//============================================================================
	//	ProjectAssetSource enum class
	//============================================================================
	enum class ProjectAssetSource : uint8_t {

		Engine,
		Game,
	};

	//============================================================================
	//	ProjectAssetIndex class
	//	プロジェクト内のアセットのインデックスを管理するクラス
	//============================================================================
	class ProjectAssetIndex {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectAssetIndex() = default;
		~ProjectAssetIndex() = default;

		bool Rebuild(const AssetDatabase& database, ProjectAssetSource source);

		//--------- accessor -----------------------------------------------------

		const ProjectDirectoryNode& GetRoot() const { return root_; }
		ProjectAssetSource GetSource() const { return source_; }
		const ProjectDirectoryNode* FindDirectory(const std::string& virtualPath) const;
		const ProjectAssetEntry* FindAssetByPath(const std::string& assetPath) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクトのルートディレクトリノード
		ProjectDirectoryNode root_{};
		// 公開済みの一覧が属するソース
		ProjectAssetSource source_ = ProjectAssetSource::Engine;

		//--------- functions ----------------------------------------------------

		// 構築用の索引へファイル一覧を収集する
		void BuildRoot(const AssetDatabase& database, ProjectAssetSource source);

		// ブラウザ上に表示しないファイルかを判定する
		static bool ShouldHideInBrowser(const std::filesystem::path& fullPath);
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
