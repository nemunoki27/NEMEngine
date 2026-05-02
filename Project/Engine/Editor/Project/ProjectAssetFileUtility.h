#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Project/ProjectAssetIndex.h>

// c++
#include <cstdint>
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	ProjectAssetFileKind enum class
	//============================================================================
	enum class ProjectAssetFileKind :
		uint8_t {

		Folder,
		Text,
		Script,
		Scene,
		Prefab,
		Material,
		Animation,
		Shader,
		RenderPipeline,
	};

	//============================================================================
	//	ProjectAssetFileResult structure
	//============================================================================
	struct ProjectAssetFileResult {

		bool success = false;
		bool isDirectory = false;

		std::string assetPath;
		std::filesystem::path fullPath;
		std::string message;
	};

	//============================================================================
	//	ProjectAssetFileUtility class
	//	プロジェクトパネルで使用するファイル操作ユーティリティ
	//============================================================================
	class ProjectAssetFileUtility {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetFileUtility() = delete;
		~ProjectAssetFileUtility() = delete;

		// 作成メニューに表示する名前を取得する
		static const char* GetCreateMenuLabel(ProjectAssetFileKind kind);
		// 新規作成時のデフォルト名を取得する
		static const char* GetDefaultName(ProjectAssetFileKind kind);

		// 指定ディレクトリにアセットまたはフォルダを作成する
		static ProjectAssetFileResult Create(ProjectAssetSource source,
			const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName);
		// 指定アセットを同じディレクトリに複製する
		static ProjectAssetFileResult DuplicateAsset(const ProjectAssetEntry& asset);
		// 指定ディレクトリを同じ階層に複製する
		static ProjectAssetFileResult DuplicateDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
		// 指定アセットを削除する
		static ProjectAssetFileResult DeleteAsset(const ProjectAssetEntry& asset);
		// 指定ディレクトリを削除する
		static ProjectAssetFileResult DeleteDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
		// 指定アセットを別ディレクトリへ移動する
		static ProjectAssetFileResult MoveAsset(const ProjectAssetEntry& asset, ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath);
		// 指定ディレクトリを別ディレクトリへ移動する
		static ProjectAssetFileResult MoveDirectory(ProjectAssetSource source, const std::string& sourceDirectoryVirtualPath,
			const std::string& targetDirectoryVirtualPath);

		//--------- accessor -----------------------------------------------------

		// ソースに対応する実ファイル上のルートを取得する
		static std::filesystem::path GetSourceRoot(ProjectAssetSource source);
		// 仮想ディレクトリパスから実ディレクトリパスを取得する
		static std::filesystem::path ResolveVirtualDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 仮想ルート名を取得する
		static const char* GetSourceVirtualRoot(ProjectAssetSource source);
		// ファイル作成用の拡張子を取得する
		static const char* GetFileSuffix(ProjectAssetFileKind kind);
		// 作成するファイルの中身を構築する
		static std::string BuildFileContent(ProjectAssetFileKind kind, const std::string& assetName);
	};
} // Engine
