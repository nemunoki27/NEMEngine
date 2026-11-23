#include "RuntimePathResolution.h"

//============================================================================
//	include
//============================================================================

#include <Engine/Core/Assets/Package/ProductContentPack.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <stdexcept>

namespace Engine::RuntimePathDetail {

	void PrepareRuntimePaths(RuntimePaths::PathState& state) {

		std::error_code ec;
		std::filesystem::create_directories(state.userSettingsRoot, ec);
		std::filesystem::create_directories(state.libraryRoot, ec);
		std::filesystem::create_directories(state.savedRoot, ec);
		if (ProductContentPack::IsMounted()) {
			nlohmann::json lock;
			if (!ProductContentPack::ReadJson(state.packagesRoot / "packages-lock.json", lock) ||
				!lock.contains("dependencies") || !lock["dependencies"].is_object()) {
				throw std::runtime_error("Invalid product package registry");
			}
			for (const auto& [name, entry] : lock["dependencies"].items()) {
				const auto folder = Algorithm::PathFromUTF8(name);
				if (!IsChildPath(folder) || folder.has_parent_path()) { throw std::runtime_error("Invalid product package name"); }
				state.packages.emplace_back(name, entry.at("version").get<std::string>(), "embedded",
					state.packagesRoot / folder, std::stoull(entry.at("contentHash").get<std::string>(), nullptr, 16));
			}
			return;
		}
		std::filesystem::create_directories(state.projectSettingsRoot, ec);
		std::filesystem::create_directories(state.packagesRoot, ec);
		const PackageResolveResult packageResult = PackageResolver::Resolve(
			state.gameRoot, state.packagesRoot, state.libraryRoot);
		state.packages = packageResult.packages;
		state.packageIssues = packageResult.issues;
	}
}
