#pragma once

#include <filesystem>

namespace Engine::ManagedRuntimePaths {

	// 実行配置から候補を順に探索する
	std::filesystem::path ResolveScriptCoreAssemblyPath();
	// 実行配置から候補を順に探索する
	std::filesystem::path ResolveGameAssemblyPath();
	// 実行配置から候補を順に探索する
	std::filesystem::path ResolveGameScriptProjectPath();
}
