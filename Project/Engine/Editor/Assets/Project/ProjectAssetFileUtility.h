#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

// c++
#include <cstdint>
#include <memory>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

	class SceneAssetStorage;

	//============================================================================
	//	ProjectAssetFileUtility class
	//	プロジェクトパネルで使用するファイル操作ユーティリティ
	//============================================================================
	class ProjectAssetFileUtility {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectAssetFileUtility() = delete;
		~ProjectAssetFileUtility() = delete;

		// 作成メニューに表示する名前を取得する
		static const char* GetCreateMenuLabel(ProjectAssetFileKind kind);
		// 新規作成時のデフォルト名を取得する
		static const char* GetDefaultName(ProjectAssetFileKind kind);
		// リネーム入力で編集できるファイル名部分を取得する
		static std::string GetEditableAssetName(const ProjectAssetEntry& asset);
		// リネーム時に保護するファイル名サフィックスを取得する
		static std::string GetProtectedAssetSuffix(const ProjectAssetEntry& asset);

		// 指定ディレクトリにアセットまたはフォルダを作成する
		static ProjectAssetFileResult Create(ProjectAssetSource source,
			const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName);
		// 指定アセットを同じディレクトリに複製する
		static ProjectAssetFileResult DuplicateAsset(const ProjectAssetEntry& asset,
			const std::shared_ptr<SceneAssetStorage>& storage);
		// 指定アセットを別ディレクトリへコピーする、コピペ操作で使う
		static ProjectAssetFileResult CopyAsset(const ProjectAssetEntry& asset, ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath,
			const std::shared_ptr<SceneAssetStorage>& storage);
		// 外部エクスプローラーからドロップされたファイルを指定ディレクトリへ取り込む
		static ProjectAssetFileResult ImportExternalFile(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath);
		// 外部エクスプローラーからドロップされたフォルダを中身ごと指定ディレクトリへ取り込む
		static ProjectAssetFileResult ImportExternalDirectory(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath);
		// 指定アセットの保護サフィックスより前の名前を変更する
		static ProjectAssetFileResult RenameAsset(const ProjectAssetEntry& asset, const std::string& requestedName);
		// 指定ディレクトリの名前を変更する
		static ProjectAssetFileResult RenameDirectory(ProjectAssetSource source,
			const std::string& directoryVirtualPath, const std::string& requestedName);
		// 指定ディレクトリを同じ階層に複製する
		static ProjectAssetFileResult DuplicateDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath,
			const std::shared_ptr<SceneAssetStorage>& storage);
		// 指定アセットを削除する
		static ProjectAssetFileResult DeleteAsset(const ProjectAssetEntry& asset, const AssetDatabase& database,
			const std::shared_ptr<SceneAssetStorage>& storage);
		// 指定ディレクトリを削除する
		static ProjectAssetFileResult DeleteDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath, const AssetDatabase& database,
			const std::shared_ptr<SceneAssetStorage>& storage);
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
	};
} // Engine
