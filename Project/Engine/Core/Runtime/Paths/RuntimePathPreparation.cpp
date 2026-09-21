#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================

namespace Engine::RuntimePathDetail {

	void PrepareRuntimePaths(RuntimePaths::PathState& state) {

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
	}
}
