#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

namespace Engine::RuntimePathDetail {

	bool IsEngineProjectRoot(const std::filesystem::path& path) {

		return ExistsDirectory(path / "Engine/Assets");
	}

	std::filesystem::path MakeEngineProjectRoot(const std::filesystem::path& path) {

		if (IsEngineProjectRoot(path)) {
			return NormalizePath(path);
		}
		if (IsEngineProjectRoot(path / "Project")) {
			return NormalizePath(path / "Project");
		}
		return {};
	}

	std::filesystem::path FindEngineProjectRootFromEnvironment() {

		// 環境で指定されたEngineの配置を解決
		const auto root = Algorithm::GetEnvironmentPath(L"NEMENGINE_ROOT");
		if (root.empty()) {
			return {};
		}
		std::filesystem::path result = MakeEngineProjectRoot(root);
		if (!result.empty()) {
			return result;
		}
		return {};
	}

	std::filesystem::path FindEngineProjectRoot(const std::filesystem::path& projectRoot) {

		if (std::filesystem::path result = MakeEngineProjectRoot(projectRoot); !result.empty()) {
			return result;
		}
		if (std::filesystem::path result = FindEngineProjectRootFromEnvironment(); !result.empty()) {
			return result;
		}

		for (std::filesystem::path current = projectRoot; !current.empty(); current = current.parent_path()) {

			const std::filesystem::path sibling = current / "NEMEngine";
			if (std::filesystem::path result = MakeEngineProjectRoot(sibling); !result.empty()) {
				return result;
			}

			const std::filesystem::path external = current / "External/NEMEngine";
			if (std::filesystem::path result = MakeEngineProjectRoot(external); !result.empty()) {
				return result;
			}

			if (current == current.parent_path()) {
				break;
			}
		}
		return projectRoot;
	}

	std::filesystem::path GetExecutablePath() {

		// OSから取得した配置パスを正規化
		const auto executable = Algorithm::GetExecutablePath();
		return executable.empty() ? std::filesystem::path{} : NormalizePath(executable);
	}

	std::filesystem::path FindGameRoot(const std::filesystem::path& descriptorPath) {

		if (!descriptorPath.empty()) {
			return descriptorPath.parent_path();
		}
		return {};
	}

	std::filesystem::path GetEnvironmentPath(const wchar_t* name) {

		return Algorithm::GetEnvironmentPath(name);
	}

	std::filesystem::path BuildUserSettingsRoot(const std::filesystem::path& gameRoot,
		const std::string& projectGUID) {

		if (const std::filesystem::path explicitRoot =
			GetEnvironmentPath(L"NEMENGINE_USER_SETTINGS_ROOT"); !explicitRoot.empty()) {
			return NormalizePath(explicitRoot / projectGUID);
		}

		// Portable設定はProject内の保存先を使用
		const auto portable = Algorithm::GetEnvironmentPath(L"NEMENGINE_PORTABLE");
		const bool usePortable = portable.native() == L"1" || portable.native() == L"true";
		if (usePortable) {
			return gameRoot / "UserSettings";
		}

		if (const std::filesystem::path localAppData = GetEnvironmentPath(L"LOCALAPPDATA");
			!localAppData.empty()) {
			return localAppData / "NEMEngine" / "Projects" / projectGUID;
		}
		return gameRoot / "UserSettings";
	}
}
