#pragma once

//============================================================================
//	include
//============================================================================
#include "ProjectAssetIndex.h"

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
		AnimationClip,
		Shader,
		RenderPipeline,
		ShaderGraph,
		RenderFeatureProfile,
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

}
