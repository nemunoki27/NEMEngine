#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

// c++
#include <memory>

namespace Engine {

	// front
	class SceneAssetStorage;

	//============================================================================
	//	ProjectAssetCopyUtility class
	//	Assetの複製と外部ファイルの取り込みを行う
	//============================================================================
	class ProjectAssetCopyUtility {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetCopyUtility() = delete;
		~ProjectAssetCopyUtility() = delete;

		// 同じフォルダーへAssetを複製する
		static ProjectAssetFileResult DuplicateAsset(
			const ProjectAssetEntry& asset, const std::shared_ptr<SceneAssetStorage>& storage);
		// 別のフォルダーへAssetをコピーする
		static ProjectAssetFileResult CopyAsset(const ProjectAssetEntry& asset, ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::shared_ptr<SceneAssetStorage>& storage);
		// Sceneの所有Actorも含めてフォルダーを複製する
		static ProjectAssetFileResult DuplicateDirectory(ProjectAssetSource source, const std::string& directoryVirtualPath,
			const std::shared_ptr<SceneAssetStorage>& storage);
		// 外部ファイルを取り込む
		static ProjectAssetFileResult ImportExternalFile(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath);
		// 外部フォルダーを取り込む
		static ProjectAssetFileResult ImportExternalDirectory(ProjectAssetSource targetSource,
			const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath);
	};
}
