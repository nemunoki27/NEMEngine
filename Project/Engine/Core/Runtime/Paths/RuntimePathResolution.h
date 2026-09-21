#pragma once

//============================================================================
//	include
//============================================================================
#include "RuntimePaths.h"

namespace Engine::RuntimePathDetail {

	// 探索・環境選択・初期化を共通の解決結果へまとめる
	bool ExistsDirectory(const std::filesystem::path& path);

	std::filesystem::path NormalizePath(const std::filesystem::path& path);

	bool IsChildPath(const std::filesystem::path& relative);

	std::filesystem::path TryMakeRelative(const std::filesystem::path& fullPath, const std::filesystem::path& root);

	std::filesystem::path FindProjectDescriptorIn(const std::filesystem::path& directory);

	std::filesystem::path FindProjectDescriptor(const std::filesystem::path& start);

	std::filesystem::path FindNamedProjectDescriptor(
		const std::filesystem::path& start, const std::wstring& projectName);

	bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath,
		std::string& outGUID,
		std::string& outName, std::filesystem::path& outAssetsDirectory,
		std::filesystem::path& outPackagesDirectory,
		std::filesystem::path& outProjectSettingsDirectory,
		Engine::SceneStorageMode& outSceneStorageMode);

	bool IsEngineProjectRoot(const std::filesystem::path& path);

	std::filesystem::path MakeEngineProjectRoot(const std::filesystem::path& path);

	std::filesystem::path FindEngineProjectRootFromEnvironment();

	std::filesystem::path FindEngineProjectRoot(const std::filesystem::path& projectRoot);

	std::filesystem::path GetExecutablePath();

	std::filesystem::path FindGameRoot(const std::filesystem::path& descriptorPath);

	std::filesystem::path GetEnvironmentPath(const wchar_t* name);

	std::filesystem::path BuildUserSettingsRoot(const std::filesystem::path& gameRoot,
		const std::string& projectGUID);

	RuntimePaths::PathState BuildResolvedPaths();

	void PrepareRuntimePaths(RuntimePaths::PathState& state);
}
