#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

namespace Engine::GameBuildUtility {
	bool IsReservedWindowsName(const std::string& name);
	bool ResolveProductName(const std::string& input, std::string& outName, std::string& outError);
	nlohmann::json LoadJson(const std::filesystem::path& path);
	bool StartsWith(const std::string& text, const char* prefix);
	std::filesystem::path NormalizeBuildPath(const std::filesystem::path& path);
	std::filesystem::path ResolveGameBuildRoot(const std::filesystem::path& gameRoot);
	std::filesystem::path ResolveGameBuildScript(const std::filesystem::path& buildRoot);
	bool IsEditorOnlyAsset(const std::string& assetPath);
	bool IsGameEditorOnlyAsset(const std::string& assetPath);
}
