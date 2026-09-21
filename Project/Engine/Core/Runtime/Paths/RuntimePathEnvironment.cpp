#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================
#include <cstdlib>
#include <Windows.h>

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

		wchar_t* root = nullptr;
		size_t rootLength = 0;
		if (_wdupenv_s(&root, &rootLength, L"NEMENGINE_ROOT") != 0 || root == nullptr) {
			return {};
		}

		std::filesystem::path result = MakeEngineProjectRoot(root);
		std::free(root);
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

		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;) {

			const DWORD length = ::GetModuleFileNameW(
				nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0) {
				return {};
			}
			if (length < buffer.size() - 1) {
				return NormalizePath(std::filesystem::path(buffer.data(), buffer.data() + length));
			}
			buffer.resize(buffer.size() * 2);
		}
	}

	std::filesystem::path FindGameRoot(const std::filesystem::path& descriptorPath) {

		if (!descriptorPath.empty()) {
			return descriptorPath.parent_path();
		}
		return {};
	}

	std::filesystem::path GetEnvironmentPath(const wchar_t* name) {

		wchar_t* value = nullptr;
		size_t length = 0;
		if (_wdupenv_s(&value, &length, name) != 0 || !value) {
			return {};
		}
		std::filesystem::path result(value);
		std::free(value);
		return result;
	}

	std::filesystem::path BuildUserSettingsRoot(const std::filesystem::path& gameRoot,
		const std::string& projectGUID) {

		if (const std::filesystem::path explicitRoot =
			GetEnvironmentPath(L"NEMENGINE_USER_SETTINGS_ROOT"); !explicitRoot.empty()) {
			return NormalizePath(explicitRoot / projectGUID);
		}

		wchar_t* portable = nullptr;
		size_t portableLength = 0;
		const bool usePortable = _wdupenv_s(&portable, &portableLength,
			L"NEMENGINE_PORTABLE") == 0 && portable &&
		(std::wstring_view(portable) == L"1" || std::wstring_view(portable) == L"true");
		std::free(portable);
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
