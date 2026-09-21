#include "RuntimePaths.h"

//============================================================================
//	include
//============================================================================
#include "RuntimePathResolution.h"
#include "RuntimeAssetPaths.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <mutex>

namespace {

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

Engine::SceneStorageMode Engine::RuntimePaths::GetSceneStorageMode() {

	return GetState().sceneStorageMode;
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

bool Engine::RuntimePaths::IsProductBuild() {

	std::error_code ec;
	return std::filesystem::is_regular_file(
		GetGameRoot() / ".nemBuildManifest.json", ec);
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

	if (!virtualPath.starts_with("engine://") && !virtualPath.starts_with("game://") &&
		!virtualPath.starts_with("library://") && !virtualPath.starts_with("user://") &&
		!virtualPath.starts_with("package://")) {
		return {};
	}
	return RuntimePathDetail::ResolveVirtualPath(GetState(), virtualPath);
}

std::string Engine::RuntimePaths::ToVirtualPath(const std::filesystem::path& fullPath) {

	if (fullPath.empty()) {
		return {};
	}
	return RuntimePathDetail::ToVirtualPath(GetState(), fullPath);
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const std::filesystem::path& assetPath) {

	if (assetPath.empty()) {
		return {};
	}
	return RuntimePathDetail::ResolveAssetPath(GetState(), assetPath);
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
	return RuntimePathDetail::ToAssetPath(GetState(), fullPath);
}

const Engine::RuntimePaths::PathState& Engine::RuntimePaths::GetState() {

	std::call_once(g_initializeOnce, []() {

		g_pathState = BuildState();
		});
	return g_pathState;
}

Engine::RuntimePaths::PathState Engine::RuntimePaths::BuildState() {

	PathState state = RuntimePathDetail::BuildResolvedPaths();
	RuntimePathDetail::PrepareRuntimePaths(state);
	return state;
}
