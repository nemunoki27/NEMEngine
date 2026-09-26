#include "PackageResolver.h"

//============================================================================
//	include
//============================================================================
#include "PackageResolution.h"

#include <algorithm>
#include <unordered_map>

using namespace Engine::PackageDetail;

namespace {

	constexpr uint32_t kPackageManifestSchemaVersion = 1;
}

Engine::PackageResolveResult Engine::PackageResolver::Resolve(
	const std::filesystem::path& projectRoot,
	const std::filesystem::path& packagesRoot,
	const std::filesystem::path& libraryRoot) {

	PackageResolveResult result{};
	const std::filesystem::path manifestPath = packagesRoot / "manifest.json";
	const nlohmann::json manifest = LoadJson(manifestPath);
	if (!manifest.is_object()) {
		AddIssue(result, {}, "Packages/manifest.json is missing or invalid");
		return result;
	}
	if (manifest.value("schemaVersion", 0u) != kPackageManifestSchemaVersion) {
		AddIssue(result, {}, "unsupported package manifest schemaVersion");
		return result;
	}

	const auto dependenciesIt = manifest.find("dependencies");
	if (dependenciesIt == manifest.end() || !dependenciesIt->is_object()) {
		AddIssue(result, {}, "package manifest dependencies must be an object");
		return result;
	}

	std::vector<DependencySpec> pending;
	for (auto it = dependenciesIt->begin(); it != dependenciesIt->end(); ++it) {
		if (std::optional<DependencySpec> spec =
			ParseDependency(it.key(), *it, projectRoot, result)) {
			spec->dependencyChain.emplace_back(spec->name);
			pending.emplace_back(std::move(*spec));
		}
	}

	std::unordered_map<std::string, size_t> resolvedByName;
	for (size_t index = 0; index < pending.size(); ++index) {

		// 推移依存の追加によるvector再確保から現在の要求を守る
		const DependencySpec spec = pending[index];
		const std::filesystem::path root = ResolvePackageRoot(spec, packagesRoot);
		const nlohmann::json packageManifest = LoadJson(root / "package.json");
		if (!packageManifest.is_object()) {
			AddIssue(result, spec.name, "package.json is missing or invalid");
			continue;
		}

		const std::string actualName = packageManifest.value("name", std::string{});
		const std::string actualVersion = packageManifest.value("version", std::string{});
		if (actualName != spec.name) {
			AddIssue(result, spec.name, "package.json name does not match dependency name");
			continue;
		}
		if (actualVersion.empty()) {
			AddIssue(result, spec.name, "package.json version is missing");
			continue;
		}
		if (!spec.version.empty() && spec.version != "*" && spec.version != actualVersion) {
			AddIssue(result, spec.name, "package version does not match requested version");
			continue;
		}

		if (const auto found = resolvedByName.find(spec.name); found != resolvedByName.end()) {

			const ResolvedPackage& existing = result.packages[found->second];
			if (existing.version != actualVersion || existing.root != root) {
				AddIssue(result, spec.name, "conflicting transitive package dependency");
			}
			continue;
		}

		// 全ファイルを読み取れたPackageだけを登録する
		const auto contentHash = ComputePackageHash(root);
		if (!contentHash) {
			AddIssue(result, spec.name, "failed to read package content");
			continue;
		}
		resolvedByName.emplace(spec.name, result.packages.size());
		result.packages.emplace_back(
			spec.name, actualVersion, spec.source, root, *contentHash);

		const auto transitiveIt = packageManifest.find("dependencies");
		if (transitiveIt == packageManifest.end()) {
			continue;
		}
		if (!transitiveIt->is_object()) {
			AddIssue(result, spec.name, "package dependencies must be an object");
			continue;
		}
		for (auto it = transitiveIt->begin(); it != transitiveIt->end(); ++it) {
			if (std::optional<DependencySpec> dependency =
				ParseDependency(it.key(), *it, root, result)) {

				if (std::find(spec.dependencyChain.begin(), spec.dependencyChain.end(),
					dependency->name) != spec.dependencyChain.end()) {
					AddIssue(result, dependency->name, "cyclic package dependency");
					continue;
				}
				dependency->dependencyChain = spec.dependencyChain;
				dependency->dependencyChain.emplace_back(dependency->name);
				pending.emplace_back(std::move(*dependency));
			}
		}
	}

	std::sort(result.packages.begin(), result.packages.end(),
		[](const ResolvedPackage& lhs, const ResolvedPackage& rhs) {
		return lhs.name < rhs.name;
		});

	std::error_code ec;
	std::filesystem::create_directories(libraryRoot / "Packages", ec);
	if (ec) {
		AddIssue(result, {}, "failed to create Library/Packages");
	}
	if (result.Succeeded() &&
		!SaveLockFile(packagesRoot / "packages-lock.json", projectRoot, result.packages)) {
		AddIssue(result, {}, "failed to write Packages/packages-lock.json");
	}
	return result;
}
