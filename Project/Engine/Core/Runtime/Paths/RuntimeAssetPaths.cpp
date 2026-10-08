#include "RuntimeAssetPaths.h"

//============================================================================
//	include
//============================================================================
#include "RuntimePathResolution.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace Engine::RuntimePathDetail {

	bool StartsWith(const std::string& text, const char* prefix) {

		return text.rfind(prefix, 0) == 0;
	}

	std::filesystem::path ResolveVirtualPath(const RuntimePaths::PathState& state, std::string_view virtualPath) {

		const auto ResolveRelative = [](const std::filesystem::path& root,
			std::string_view relativeText) -> std::filesystem::path {

			const std::filesystem::path relative = Algorithm::PathFromUTF8(std::string(relativeText));
			if (relative.empty()) {
				return root;
			}
			if (!IsChildPath(relative)) {
				return {};
			}
			return (root / relative).lexically_normal();
		};

		constexpr std::string_view kEngineScheme = "engine://";
		constexpr std::string_view kGameScheme = "game://";
		constexpr std::string_view kLibraryScheme = "library://";
		constexpr std::string_view kUserScheme = "user://";
		constexpr std::string_view kPackageScheme = "package://";

		if (virtualPath.starts_with(kEngineScheme)) {
			return ResolveRelative(state.engineAssetsRoot, virtualPath.substr(kEngineScheme.size()));
		}
		if (virtualPath.starts_with(kGameScheme)) {
			return ResolveRelative(state.gameAssetsRoot, virtualPath.substr(kGameScheme.size()));
		}
		if (virtualPath.starts_with(kLibraryScheme)) {
			return ResolveRelative(state.libraryRoot, virtualPath.substr(kLibraryScheme.size()));
		}
		if (virtualPath.starts_with(kUserScheme)) {
			return ResolveRelative(state.userSettingsRoot, virtualPath.substr(kUserScheme.size()));
		}
		if (virtualPath.starts_with(kPackageScheme)) {

			const std::string_view packagePath = virtualPath.substr(kPackageScheme.size());
			const size_t separator = packagePath.find('/');
			const std::string_view packageName = packagePath.substr(0, separator);
			const std::string_view relative = separator == std::string_view::npos ?
			std::string_view{} : packagePath.substr(separator + 1);
			for (const ResolvedPackage& package : state.packages) {
				if (package.name == packageName) {
					return ResolveRelative(package.root, relative);
				}
			}
		}
		return {};
	}

	std::string ToVirtualPath(const RuntimePaths::PathState& state, const std::filesystem::path& fullPath) {

		if (fullPath.empty()) {
			return {};
		}
		const std::filesystem::path normalized = NormalizePath(fullPath);
		const auto MakeVirtualPath = [&normalized](const char* scheme,
			const std::filesystem::path& root) -> std::string {

			const std::filesystem::path relative = TryMakeRelative(normalized, root);
			if (relative.empty()) {
				return {};
			}
			return std::string(scheme) + Algorithm::PathToUTF8(relative);
		};

		for (const ResolvedPackage& package : state.packages) {
			if (std::string path = MakeVirtualPath(
				("package://" + package.name + "/").c_str(), package.root); !path.empty()) {
				return path;
			}
		}
		if (std::string path = MakeVirtualPath("engine://", state.engineAssetsRoot); !path.empty()) {
			return path;
		}
		if (std::string path = MakeVirtualPath("game://", state.gameAssetsRoot); !path.empty()) {
			return path;
		}
		if (std::string path = MakeVirtualPath("library://", state.libraryRoot); !path.empty()) {
			return path;
		}
		if (std::string path = MakeVirtualPath("user://", state.userSettingsRoot); !path.empty()) {
			return path;
		}
		return {};
	}

	std::filesystem::path ResolveAssetPath(const RuntimePaths::PathState& state, const std::filesystem::path& assetPath) {

		if (assetPath.empty()) {
			return {};
		}
		if (assetPath.is_absolute()) {

			const std::filesystem::path normalized = NormalizePath(assetPath);
			if (std::string logicalPath = ToAssetPath(state, normalized); !logicalPath.empty()) {
				return ResolveAssetPath(state, Algorithm::PathFromUTF8(logicalPath));
			}
			return normalized.lexically_normal();
		}

		const std::string generic = Algorithm::ConvertString(assetPath.generic_wstring());
		if (const std::filesystem::path virtualPath = ResolveVirtualPath(state, generic); !virtualPath.empty()) {
			return virtualPath;
		}
		if (StartsWith(generic, "Engine/")) {
			return (state.engineProjectRoot / assetPath).lexically_normal();
		}
		if (StartsWith(generic, "GameAssets/")) {
			return (state.gameRoot / assetPath).lexically_normal();
		}

		const std::filesystem::path projectPath = (state.projectRoot / assetPath).lexically_normal();
		if (std::filesystem::exists(projectPath)) {
			return projectPath;
		}

		const std::filesystem::path enginePath = (state.engineProjectRoot / assetPath).lexically_normal();
		if (std::filesystem::exists(enginePath)) {
			return enginePath;
		}
		return projectPath;
	}

	std::string ToAssetPath(const RuntimePaths::PathState& state, const std::filesystem::path& fullPath) {

		if (fullPath.empty()) {
			return {};
		}

		const std::filesystem::path normalized = NormalizePath(fullPath);
		for (const ResolvedPackage& package : state.packages) {

			if (std::filesystem::path relative = TryMakeRelative(normalized, package.root); !relative.empty()) {
				return "package://" + package.name + "/" + Algorithm::PathToUTF8(relative);
			}
		}

		if (std::filesystem::path relative = TryMakeRelative(normalized, state.engineProjectRoot); !relative.empty()) {

			const std::string assetPath = Algorithm::ConvertString(relative.generic_wstring());
			if (StartsWith(assetPath, "Engine/Assets/")) {
				return assetPath;
			}
		}
		if (std::filesystem::path relative = TryMakeRelative(normalized, state.gameRoot); !relative.empty()) {

			const std::string assetPath = Algorithm::ConvertString(relative.generic_wstring());
			if (StartsWith(assetPath, "GameAssets/")) {
				return assetPath;
			}
		}
		if (std::filesystem::path relative = TryMakeRelative(normalized, state.projectRoot); !relative.empty()) {

			return Algorithm::ConvertString(relative.generic_wstring());
		}
		return {};
	}
}
