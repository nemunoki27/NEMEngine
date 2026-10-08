#include "ManagedRuntimePaths.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <vector>

//============================================================================
//	ManagedRuntimePaths functions
//============================================================================
namespace Engine::ManagedRuntimePaths {

	// 現在の構成名を取得
	std::string GetBuildProfile() {

		return _PROFILE;
	}

	// 優先順で最初に存在するパスを取得
	std::filesystem::path FindFirstExistingPath(const std::vector<std::filesystem::path>& paths) {

		for (const auto& path : paths) {
			if (std::filesystem::exists(path)) {
				return path;
			}
		}
		return {};
	}

	std::filesystem::path GetExecutableDirectory() {

		// EXE隣のManaged配置を探索する起点
		return Algorithm::GetExecutablePath().parent_path();
	}

	// 配布先と構成別のコアAssemblyを探索
	std::filesystem::path ResolveScriptCoreAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		const std::filesystem::path exeDir = GetExecutableDirectory();
		const std::filesystem::path engineRoot = Engine::RuntimePaths::GetEngineProjectRoot().parent_path();
		return FindFirstExistingPath({
			exeDir / "Managed/NEM.ScriptCore.dll",
			engineRoot / "Generated/Managed/NEM.ScriptCore" / profile / "NEM.ScriptCore.dll",
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "NEM.ScriptCore.dll",
			current / "Managed/NEM.ScriptCore.dll"
			});
	}

	// 配布先と構成別のゲームAssemblyを探索
	std::filesystem::path ResolveGameAssemblyPath() {

		const std::string profile = GetBuildProfile();
		const std::filesystem::path current = std::filesystem::current_path();
		const std::filesystem::path exeDir = GetExecutableDirectory();
		return FindFirstExistingPath({
			exeDir / "Managed/GameScripts.dll",
			Engine::RuntimePaths::GetGameRoot() / "Managed" / profile / "GameScripts.dll",
			current / "Managed/GameScripts.dll"
			});
	}

	// ゲーム側のScriptプロジェクトを探索
	std::filesystem::path ResolveGameScriptProjectPath() {

		const std::filesystem::path current = std::filesystem::current_path();
		return FindFirstExistingPath({
			current / "Scripts/GameScripts.csproj",
			Engine::RuntimePaths::GetGameRoot() / "Scripts/GameScripts.csproj"
			});
	}
}
