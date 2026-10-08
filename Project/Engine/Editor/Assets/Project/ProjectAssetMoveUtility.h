#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetFileTypes.h"

namespace Engine {

	//============================================================================
	//	ProjectAssetMoveUtility class
	//	Assetと付随ファイルの名前変更と移動を行う
	//============================================================================
	class ProjectAssetMoveUtility {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetMoveUtility() = delete;
		~ProjectAssetMoveUtility() = delete;

		// Assetと付随ファイルを改名する
		static ProjectAssetFileResult RenameAsset(const ProjectAssetEntry& asset, const std::string& requestedName);
		// フォルダーを改名する
		static ProjectAssetFileResult RenameDirectory(
			ProjectAssetSource source, const std::string& directoryVirtualPath, const std::string& requestedName);
		// Assetと付随ファイルを移動する
		static ProjectAssetFileResult MoveAsset(
			const ProjectAssetEntry& asset, ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath);
		// フォルダーを移動する
		static ProjectAssetFileResult MoveDirectory(ProjectAssetSource source, const std::string& sourceDirectoryVirtualPath,
			const std::string& targetDirectoryVirtualPath);
	};
}
