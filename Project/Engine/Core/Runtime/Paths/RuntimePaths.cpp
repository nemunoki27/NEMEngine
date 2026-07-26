#include "RuntimePaths.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>
// json
#include <json.hpp>

//============================================================================
//	RuntimePaths classMethods
//============================================================================
namespace {

	// パスが存在するディレクトリとして扱えるか
	bool ExistsDirectory(const std::filesystem::path& path) {

		std::error_code ec;
		return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
	}

	// パスを正規化する
	std::filesystem::path NormalizePath(const std::filesystem::path& path) {

		std::error_code ec;
		std::filesystem::path result = std::filesystem::weakly_canonical(path, ec);
		if (!ec) {
			return result;
		}
		return path.lexically_normal();
	}

	// Engine/Assetsを持つProjectディレクトリかどうか
	bool IsEngineProjectRoot(const std::filesystem::path& path) {

		return ExistsDirectory(path / "Engine/Assets");
	}

	// NEMEngineルート、もしくはProjectルートからEngineのProjectルートを取得
	std::filesystem::path MakeEngineProjectRoot(const std::filesystem::path& path) {

		if (IsEngineProjectRoot(path)) {
			return NormalizePath(path);
		}
		if (IsEngineProjectRoot(path / "Project")) {
			return NormalizePath(path / "Project");
		}
		return {};
	}

	// 環境変数からEngineのProjectルートを取得
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

	// 実行中プロジェクトからEngineのProjectルートを探索
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

	// 指定ディレクトリ直下のプロジェクト記述子を取得
	std::filesystem::path FindProjectDescriptorIn(const std::filesystem::path& directory) {

		std::error_code ec;
		if (!ExistsDirectory(directory)) {
			return {};
		}

		std::vector<std::filesystem::path> candidates;
		for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {

			if (ec) {
				break;
			}
			if (entry.is_regular_file(ec) && entry.path().extension() == ".nemproject") {
				candidates.emplace_back(entry.path());
			}
		}
		if (candidates.empty()) {
			return {};
		}
		std::sort(candidates.begin(), candidates.end());
		return NormalizePath(candidates.front());
	}

	// 実行位置からプロジェクト記述子を探索
	std::filesystem::path FindProjectDescriptor(const std::filesystem::path& start) {

		for (std::filesystem::path current = start; !current.empty(); current = current.parent_path()) {

			if (std::filesystem::path descriptor = FindProjectDescriptorIn(current); !descriptor.empty()) {
				return descriptor;
			}
			if (current == current.parent_path()) {
				break;
			}
		}

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(start, ec)) {

			if (ec) {
				break;
			}
			if (!entry.is_directory(ec)) {
				continue;
			}
			if (std::filesystem::path descriptor = FindProjectDescriptorIn(entry.path()); !descriptor.empty()) {
				return descriptor;
			}
		}
		return {};
	}

	// GameAssetsを持つゲーム側ルートを探索
	std::filesystem::path FindGameRoot(const std::filesystem::path& descriptorPath) {

		if (!descriptorPath.empty()) {
			return descriptorPath.parent_path();
		}
		return {};
	}

	// relativeが..を含まないか確認
	bool IsChildPath(const std::filesystem::path& relative) {

		if (relative.empty()) {
			return false;
		}
		for (const auto& part : relative) {

			if (part == "..") {
				return false;
			}
		}
		return true;
	}

	// UTF-16環境変数を取得
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

	// プロジェクト記述子を読み込む
	bool LoadProjectDescriptor(const std::filesystem::path& descriptorPath,
		std::string& outGUID,
		std::string& outName, std::filesystem::path& outAssetsDirectory,
		std::filesystem::path& outPackagesDirectory,
		std::filesystem::path& outProjectSettingsDirectory) {

		if (descriptorPath.empty()) {
			return false;
		}

		std::ifstream file(descriptorPath, std::ios::binary);
		const nlohmann::json data = nlohmann::json::parse(file, nullptr, false);
		if (!data.is_object() || data.value("schemaVersion", 0u) != 1u) {
			return false;
		}

		const std::string guid = data.value("projectGuid", std::string{});
		if (guid.size() != 32 || !std::all_of(guid.begin(), guid.end(), [](unsigned char c) {
			return std::isxdigit(c) != 0;
			})) {
			return false;
		}
		outGUID = Engine::Algorithm::ToLower(guid);
		outName = data.value("name", std::string{});
		if (outName.empty()) {
			return false;
		}

		const std::filesystem::path assetsDirectory =
			Engine::Algorithm::PathFromUTF8(data.value("assetsDirectory", std::string{}));
		if (assetsDirectory.empty() || assetsDirectory.is_absolute() || !IsChildPath(assetsDirectory)) {
			return false;
		}
		outAssetsDirectory = assetsDirectory;

		const std::filesystem::path packagesDirectory =
			Engine::Algorithm::PathFromUTF8(data.value("packagesDirectory", std::string{}));
		if (packagesDirectory.empty() || packagesDirectory.is_absolute() || !IsChildPath(packagesDirectory)) {
			return false;
		}
		outPackagesDirectory = packagesDirectory;

		const std::filesystem::path projectSettingsDirectory =
			Engine::Algorithm::PathFromUTF8(data.value("projectSettingsDirectory", std::string{}));
		if (projectSettingsDirectory.empty() || projectSettingsDirectory.is_absolute() ||
			!IsChildPath(projectSettingsDirectory)) {
			return false;
		}
		outProjectSettingsDirectory = projectSettingsDirectory;
		return true;
	}

	// ユーザー設定ルートを構築する
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

	// fullPathがroot配下なら相対パスを返す
	std::filesystem::path TryMakeRelative(const std::filesystem::path& fullPath, const std::filesystem::path& root) {

		std::error_code ec;
		std::filesystem::path relative = std::filesystem::relative(fullPath, root, ec);
		if (ec || !IsChildPath(relative)) {
			return {};
		}
		return relative;
	}

	bool StartsWith(const std::string& text, const char* prefix) {

		return text.rfind(prefix, 0) == 0;
	}

	Engine::RuntimePaths::PathState g_pathState{};
	std::once_flag g_initializeOnce;
}

void Engine::RuntimePaths::Refresh() {

	g_pathState = BuildState();
}

const std::filesystem::path& Engine::RuntimePaths::GetProjectRoot() {

	return GetState().projectRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetGameRoot() {

	return GetState().gameRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetEngineProjectRoot() {

	return GetState().engineProjectRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetEngineAssetsRoot() {

	return GetState().engineAssetsRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetGameAssetsRoot() {

	return GetState().gameAssetsRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetProjectDescriptorPath() {

	return GetState().projectDescriptorPath;
}

const std::string& Engine::RuntimePaths::GetProjectGUID() {

	return GetState().projectGUID;
}

const std::string& Engine::RuntimePaths::GetProjectName() {

	return GetState().projectName;
}

const std::filesystem::path& Engine::RuntimePaths::GetProjectSettingsRoot() {

	return GetState().projectSettingsRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetUserSettingsRoot() {

	return GetState().userSettingsRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetLibraryRoot() {

	return GetState().libraryRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetSavedRoot() {

	return GetState().savedRoot;
}

const std::filesystem::path& Engine::RuntimePaths::GetPackagesRoot() {

	return GetState().packagesRoot;
}

const std::vector<Engine::ResolvedPackage>& Engine::RuntimePaths::GetPackages() {

	return GetState().packages;
}

const std::vector<Engine::PackageResolveIssue>& Engine::RuntimePaths::GetPackageIssues() {

	return GetState().packageIssues;
}

std::filesystem::path Engine::RuntimePaths::GetEngineAssetPath(const std::filesystem::path& relativePath) {

	return (GetEngineAssetsRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::GetProjectSettingsPath(const std::filesystem::path& relativePath) {

	return (GetProjectSettingsRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::GetUserSettingsPath(const std::filesystem::path& relativePath) {

	return (GetUserSettingsRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::GetLibraryPath(const std::filesystem::path& relativePath) {

	return (GetLibraryRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::GetSavedPath(const std::filesystem::path& relativePath) {

	return (GetSavedRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::ResolveVirtualPath(std::string_view virtualPath) {

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
		return ResolveRelative(GetEngineAssetsRoot(), virtualPath.substr(kEngineScheme.size()));
	}
	if (virtualPath.starts_with(kGameScheme)) {
		return ResolveRelative(GetGameAssetsRoot(), virtualPath.substr(kGameScheme.size()));
	}
	if (virtualPath.starts_with(kLibraryScheme)) {
		return ResolveRelative(GetLibraryRoot(), virtualPath.substr(kLibraryScheme.size()));
	}
	if (virtualPath.starts_with(kUserScheme)) {
		return ResolveRelative(GetUserSettingsRoot(), virtualPath.substr(kUserScheme.size()));
	}
	if (virtualPath.starts_with(kPackageScheme)) {

		const std::string_view packagePath = virtualPath.substr(kPackageScheme.size());
		const size_t separator = packagePath.find('/');
		const std::string_view packageName = packagePath.substr(0, separator);
		const std::string_view relative = separator == std::string_view::npos ?
			std::string_view{} : packagePath.substr(separator + 1);
		for (const ResolvedPackage& package : GetPackages()) {
			if (package.name == packageName) {
				return ResolveRelative(package.root, relative);
			}
		}
	}
	return {};
}

std::string Engine::RuntimePaths::ToVirtualPath(const std::filesystem::path& fullPath) {

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

	for (const ResolvedPackage& package : GetPackages()) {
		if (std::string path = MakeVirtualPath(
			("package://" + package.name + "/").c_str(), package.root); !path.empty()) {
			return path;
		}
	}
	if (std::string path = MakeVirtualPath("engine://", GetEngineAssetsRoot()); !path.empty()) {
		return path;
	}
	if (std::string path = MakeVirtualPath("game://", GetGameAssetsRoot()); !path.empty()) {
		return path;
	}
	if (std::string path = MakeVirtualPath("library://", GetLibraryRoot()); !path.empty()) {
		return path;
	}
	if (std::string path = MakeVirtualPath("user://", GetUserSettingsRoot()); !path.empty()) {
		return path;
	}
	return {};
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const std::filesystem::path& assetPath) {

	if (assetPath.empty()) {
		return {};
	}
	if (assetPath.is_absolute()) {

		const std::filesystem::path normalized = NormalizePath(assetPath);
		if (std::string logicalPath = ToAssetPath(normalized); !logicalPath.empty()) {
			return ResolveAssetPath(logicalPath);
		}
		return normalized.lexically_normal();
	}

	const std::string generic = Algorithm::ConvertString(assetPath.generic_wstring());
	if (const std::filesystem::path virtualPath = ResolveVirtualPath(generic); !virtualPath.empty()) {
		return virtualPath;
	}
	if (StartsWith(generic, "Engine/")) {
		return (GetEngineProjectRoot() / assetPath).lexically_normal();
	}
	if (StartsWith(generic, "GameAssets/")) {
		return (GetGameRoot() / assetPath).lexically_normal();
	}

	const std::filesystem::path projectPath = (GetProjectRoot() / assetPath).lexically_normal();
	if (std::filesystem::exists(projectPath)) {
		return projectPath;
	}

	const std::filesystem::path enginePath = (GetEngineProjectRoot() / assetPath).lexically_normal();
	if (std::filesystem::exists(enginePath)) {
		return enginePath;
	}
	return projectPath;
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const std::string& assetPath) {

	return ResolveAssetPath(Algorithm::PathFromUTF8(assetPath));
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const char* assetPath) {

	return ResolveAssetPath(std::string(assetPath));
}

std::string Engine::RuntimePaths::ToAssetPath(const std::filesystem::path& fullPath) {

	if (fullPath.empty()) {
		return {};
	}

	const std::filesystem::path normalized = NormalizePath(fullPath);
	for (const ResolvedPackage& package : GetPackages()) {

		if (std::filesystem::path relative = TryMakeRelative(normalized, package.root); !relative.empty()) {
			return "package://" + package.name + "/" + Algorithm::PathToUTF8(relative);
		}
	}

	if (std::filesystem::path relative = TryMakeRelative(normalized, GetEngineProjectRoot()); !relative.empty()) {

		const std::string assetPath = Algorithm::ConvertString(relative.generic_wstring());
		if (StartsWith(assetPath, "Engine/Assets/")) {
			return assetPath;
		}
	}
	if (std::filesystem::path relative = TryMakeRelative(normalized, GetGameRoot()); !relative.empty()) {

		const std::string assetPath = Algorithm::ConvertString(relative.generic_wstring());
		if (StartsWith(assetPath, "GameAssets/")) {
			return assetPath;
		}
	}
	if (std::filesystem::path relative = TryMakeRelative(normalized, GetProjectRoot()); !relative.empty()) {

		return Algorithm::ConvertString(relative.generic_wstring());
	}
	return {};
}

const Engine::RuntimePaths::PathState& Engine::RuntimePaths::GetState() {

	std::call_once(g_initializeOnce, []() {

		g_pathState = BuildState();
		});
	return g_pathState;
}

Engine::RuntimePaths::PathState Engine::RuntimePaths::BuildState() {

	PathState state{};
	const std::filesystem::path launchRoot = NormalizePath(std::filesystem::current_path());
	state.projectDescriptorPath = FindProjectDescriptor(launchRoot);
	state.gameRoot = FindGameRoot(state.projectDescriptorPath);
	if (state.projectDescriptorPath.empty() || state.gameRoot.empty()) {
		throw std::runtime_error(
			"NEM project descriptor was not found from the current directory");
	}
	state.engineProjectRoot = FindEngineProjectRoot(launchRoot);
	state.projectRoot = state.gameRoot;
	state.engineAssetsRoot = state.engineProjectRoot / "Engine/Assets";

	std::filesystem::path assetsDirectory;
	std::filesystem::path packagesDirectory;
	std::filesystem::path projectSettingsDirectory;
	if (!LoadProjectDescriptor(state.projectDescriptorPath,
		state.projectGUID, state.projectName, assetsDirectory,
		packagesDirectory, projectSettingsDirectory)) {
		throw std::runtime_error("NEM project descriptor is invalid");
	}
	state.gameAssetsRoot = state.gameRoot / assetsDirectory;
	state.packagesRoot = state.gameRoot / packagesDirectory;
	state.projectSettingsRoot = state.gameRoot / projectSettingsDirectory;
	state.userSettingsRoot = BuildUserSettingsRoot(state.gameRoot, state.projectGUID);
	state.libraryRoot = state.gameRoot / "Library";
	state.savedRoot = state.gameRoot / "Saved";

	std::error_code ec;
	std::filesystem::create_directories(state.projectSettingsRoot, ec);
	std::filesystem::create_directories(state.userSettingsRoot, ec);
	std::filesystem::create_directories(state.libraryRoot, ec);
	std::filesystem::create_directories(state.savedRoot, ec);
	std::filesystem::create_directories(state.packagesRoot, ec);
	const PackageResolveResult packageResult = PackageResolver::Resolve(
		state.gameRoot, state.packagesRoot, state.libraryRoot);
	state.packages = packageResult.packages;
	state.packageIssues = packageResult.issues;
	return state;
}
