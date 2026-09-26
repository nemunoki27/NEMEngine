#include "RuntimePaths.h"

//============================================================================
//	include
//============================================================================
#include "RuntimePathResolution.h"
#include "RuntimeAssetPaths.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

#include <mutex>
#include <limits>
#include <stdexcept>

namespace {

	std::shared_ptr<const Engine::RuntimePaths::PathState> g_pathState;
	std::mutex g_pathMutex;
	std::once_flag g_initializeOnce;
}

void Engine::RuntimePaths::Refresh() {

	// 初期化を終えてから新しい集合を組み立てる
	GetSnapshot();
	auto next = std::make_shared<PathState>(BuildState());
	std::scoped_lock lock(g_pathMutex);
	if (g_pathState->revision == std::numeric_limits<uint64_t>::max()) {
		throw std::overflow_error("RuntimePaths revision exhausted");
	}
	// 古い集合は借用中の利用側が解放するまで保持する
	next->revision = g_pathState->revision + 1;
	g_pathState = std::move(next);
}

std::filesystem::path Engine::RuntimePaths::GetProjectRoot() {

	return GetSnapshot()->projectRoot;
}

std::filesystem::path Engine::RuntimePaths::GetGameRoot() {

	return GetSnapshot()->gameRoot;
}

std::filesystem::path Engine::RuntimePaths::GetEngineProjectRoot() {

	return GetSnapshot()->engineProjectRoot;
}

std::filesystem::path Engine::RuntimePaths::GetEngineAssetsRoot() {

	return GetSnapshot()->engineAssetsRoot;
}

std::filesystem::path Engine::RuntimePaths::GetGameAssetsRoot() {

	return GetSnapshot()->gameAssetsRoot;
}

std::filesystem::path Engine::RuntimePaths::GetProjectDescriptorPath() {

	return GetSnapshot()->projectDescriptorPath;
}

std::string Engine::RuntimePaths::GetProjectGUID() {

	return GetSnapshot()->projectGUID;
}

std::string Engine::RuntimePaths::GetProjectName() {

	return GetSnapshot()->projectName;
}

Engine::SceneStorageMode Engine::RuntimePaths::GetSceneStorageMode() {

	return GetSnapshot()->sceneStorageMode;
}

std::filesystem::path Engine::RuntimePaths::GetProjectSettingsRoot() {

	return GetSnapshot()->projectSettingsRoot;
}

std::filesystem::path Engine::RuntimePaths::GetUserSettingsRoot() {

	return GetSnapshot()->userSettingsRoot;
}

std::filesystem::path Engine::RuntimePaths::GetLibraryRoot() {

	return GetSnapshot()->libraryRoot;
}

std::filesystem::path Engine::RuntimePaths::GetSavedRoot() {

	return GetSnapshot()->savedRoot;
}

bool Engine::RuntimePaths::IsProductBuild() {

	std::error_code ec;
	return std::filesystem::is_regular_file(
		GetGameRoot() / ".nemBuildManifest.json", ec);
}

std::filesystem::path Engine::RuntimePaths::GetPackagesRoot() {

	return GetSnapshot()->packagesRoot;
}

std::vector<Engine::ResolvedPackage> Engine::RuntimePaths::GetPackages() {

	return GetSnapshot()->packages;
}

std::vector<Engine::PackageResolveIssue> Engine::RuntimePaths::GetPackageIssues() {

	return GetSnapshot()->packageIssues;
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
	return RuntimePathDetail::ResolveVirtualPath(*GetSnapshot(), virtualPath);
}

std::string Engine::RuntimePaths::ToVirtualPath(const std::filesystem::path& fullPath) {

	if (fullPath.empty()) {
		return {};
	}
	return RuntimePathDetail::ToVirtualPath(*GetSnapshot(), fullPath);
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const std::filesystem::path& assetPath) {

	if (assetPath.empty()) {
		return {};
	}
	return RuntimePathDetail::ResolveAssetPath(*GetSnapshot(), assetPath);
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
	return RuntimePathDetail::ToAssetPath(*GetSnapshot(), fullPath);
}

std::shared_ptr<const Engine::RuntimePaths::PathState> Engine::RuntimePaths::GetSnapshot() {

	std::call_once(g_initializeOnce, []() {

		// 初回だけ構築し、不変の集合として公開する
		auto initial = std::make_shared<PathState>(BuildState());
		initial->revision = 1;
		std::scoped_lock lock(g_pathMutex);
		g_pathState = std::move(initial);
		});
	std::scoped_lock lock(g_pathMutex);
	return g_pathState;
}

Engine::RuntimePaths::PathState Engine::RuntimePaths::BuildState() {

	PathState state = RuntimePathDetail::BuildResolvedPaths();
	RuntimePathDetail::PrepareRuntimePaths(state);
	return state;
}
