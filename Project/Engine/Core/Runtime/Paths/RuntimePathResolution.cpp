#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <stdexcept>

namespace Engine::RuntimePathDetail {

	RuntimePaths::PathState BuildResolvedPaths() {

		RuntimePaths::PathState state{};
		const std::filesystem::path launchRoot = NormalizePath(std::filesystem::current_path());
		const std::filesystem::path executablePath = GetExecutablePath();
		const std::filesystem::path executableRoot = executablePath.parent_path();
		state.projectDescriptorPath = FindProjectDescriptor(launchRoot);
		if (state.projectDescriptorPath.empty() && executableRoot != launchRoot) {
			state.projectDescriptorPath = FindProjectDescriptor(executableRoot);
		}
		if (state.projectDescriptorPath.empty()) {
			state.projectDescriptorPath = FindNamedProjectDescriptor(
				launchRoot, executablePath.stem().wstring());
		}
		if (state.projectDescriptorPath.empty() && executableRoot != launchRoot) {
			state.projectDescriptorPath = FindNamedProjectDescriptor(
				executableRoot, executablePath.stem().wstring());
		}
		state.gameRoot = FindGameRoot(state.projectDescriptorPath);
		if (state.projectDescriptorPath.empty() || state.gameRoot.empty()) {
			throw std::runtime_error("NEM project descriptor was not found. current=" +
				Algorithm::PathToUTF8(launchRoot) + " executable=" +
				Algorithm::PathToUTF8(executablePath));
		}
		state.engineProjectRoot = FindEngineProjectRoot(launchRoot);
		state.projectRoot = state.gameRoot;
		state.engineAssetsRoot = state.engineProjectRoot / "Engine/Assets";

		std::filesystem::path assetsDirectory;
		std::filesystem::path packagesDirectory;
		std::filesystem::path projectSettingsDirectory;
		if (!LoadProjectDescriptor(state.projectDescriptorPath,
			state.projectGUID, state.projectName, assetsDirectory,
			packagesDirectory, projectSettingsDirectory,
			state.sceneStorageMode)) {
			throw std::runtime_error("NEM project descriptor is invalid");
		}
		state.gameAssetsRoot = state.gameRoot / assetsDirectory;
		state.packagesRoot = state.gameRoot / packagesDirectory;
		state.projectSettingsRoot = state.gameRoot / projectSettingsDirectory;
		state.userSettingsRoot = BuildUserSettingsRoot(state.gameRoot, state.projectGUID);
		state.libraryRoot = state.gameRoot / "Library";
		state.savedRoot = state.gameRoot / "Saved";

		return state;
	}
}
