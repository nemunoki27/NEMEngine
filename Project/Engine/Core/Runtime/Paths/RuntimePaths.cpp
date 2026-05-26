#include "RuntimePaths.h"

//============================================================================
//	include
//============================================================================

// c++
#include <cstdlib>
#include <mutex>

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

		char* root = nullptr;
		size_t rootLength = 0;
		if (_dupenv_s(&root, &rootLength, "NEMENGINE_ROOT") != 0 || root == nullptr) {
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

	// GameAssetsを持つゲーム側ルートを探索
	std::filesystem::path FindGameRoot(const std::filesystem::path& projectRoot) {

		if (ExistsDirectory(projectRoot / "GameAssets")) {
			return projectRoot;
		}

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(projectRoot, ec)) {

			if (ec) {
				break;
			}
			if (!entry.is_directory()) {
				continue;
			}
			if (ExistsDirectory(entry.path() / "GameAssets")) {
				return NormalizePath(entry.path());
			}
		}
		return projectRoot;
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

const std::filesystem::path& Engine::RuntimePaths::GetEngineLibraryRoot() {

	return GetState().engineLibraryRoot;
}

std::filesystem::path Engine::RuntimePaths::GetEngineAssetPath(const std::filesystem::path& relativePath) {

	return (GetEngineAssetsRoot() / relativePath).lexically_normal();
}

std::filesystem::path Engine::RuntimePaths::ResolveAssetPath(const std::filesystem::path& assetPath) {

	if (assetPath.empty()) {
		return {};
	}
	if (assetPath.is_absolute()) {
		return assetPath.lexically_normal();
	}

	const std::string generic = assetPath.generic_string();
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

std::string Engine::RuntimePaths::ToAssetPath(const std::filesystem::path& fullPath) {

	if (fullPath.empty()) {
		return {};
	}

	const std::filesystem::path normalized = NormalizePath(fullPath);

	if (std::filesystem::path relative = TryMakeRelative(normalized, GetEngineProjectRoot()); !relative.empty()) {

		const std::string assetPath = relative.generic_string();
		if (StartsWith(assetPath, "Engine/Assets/")) {
			return assetPath;
		}
	}
	if (std::filesystem::path relative = TryMakeRelative(normalized, GetGameRoot()); !relative.empty()) {

		const std::string assetPath = relative.generic_string();
		if (StartsWith(assetPath, "GameAssets/")) {
			return assetPath;
		}
	}
	if (std::filesystem::path relative = TryMakeRelative(normalized, GetProjectRoot()); !relative.empty()) {

		return relative.generic_string();
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
	state.projectRoot = NormalizePath(std::filesystem::current_path());
	state.gameRoot = FindGameRoot(state.projectRoot);
	state.engineProjectRoot = FindEngineProjectRoot(state.projectRoot);
	state.engineAssetsRoot = state.engineProjectRoot / "Engine/Assets";
	state.engineLibraryRoot = state.engineProjectRoot / "Engine/Library";
	return state;
}
