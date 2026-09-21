#pragma once

//============================================================================
//	include
//============================================================================
#include "PackageResolver.h"

#include <optional>
#include <string_view>
#include <json.hpp>

namespace Engine::PackageDetail {

	struct DependencySpec {

		std::string name;
		std::string version;
		std::string source;
		std::filesystem::path path;
		std::filesystem::path relativeBase;
		std::vector<std::string> dependencyChain;
	};

	// 依存解決で共有するmanifest・hash・保存処理
	bool IsValidPackageName(std::string_view name);

	bool IsChildPath(const std::filesystem::path& path);

	std::filesystem::path NormalizePath(const std::filesystem::path& path);

	nlohmann::json LoadJson(const std::filesystem::path& path);

	void AddIssue(Engine::PackageResolveResult& result,
		std::string packageName, std::string detail);

	std::optional<DependencySpec> ParseDependency(std::string_view name,
		const nlohmann::json& value, const std::filesystem::path& relativeBase,
		Engine::PackageResolveResult& result);

	std::filesystem::path ResolvePackageRoot(const DependencySpec& spec,
		const std::filesystem::path& packagesRoot);

	uint64_t HashBytes(uint64_t hash, const void* data, size_t size);

	uint64_t ComputePackageHash(const std::filesystem::path& root);

	std::string ToHex(uint64_t value);

	bool SaveLockFile(const std::filesystem::path& path,
		const std::filesystem::path& projectRoot,
		const std::vector<Engine::ResolvedPackage>& packages);
}
