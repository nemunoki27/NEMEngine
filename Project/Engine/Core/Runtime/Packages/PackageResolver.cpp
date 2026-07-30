#include "PackageResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
// json
#include <json.hpp>

//============================================================================
//	PackageResolver classMethods
//============================================================================
namespace {

	constexpr uint32_t kPackageManifestSchemaVersion = 1;
	constexpr uint32_t kPackageLockSchemaVersion = 1;
	constexpr uint64_t kFnvOffset = 1469598103934665603ull;
	constexpr uint64_t kFnvPrime = 1099511628211ull;

	struct DependencySpec {

		std::string name;
		std::string version;
		std::string source;
		std::filesystem::path path;
		std::filesystem::path relativeBase;
		std::vector<std::string> dependencyChain;
	};

	bool IsValidPackageName(std::string_view name) {

		if (name.empty() || name.front() == '.' || name.back() == '.') {
			return false;
		}
		return std::all_of(name.begin(), name.end(), [](unsigned char character) {
			return std::islower(character) || std::isdigit(character) ||
				character == '.' || character == '-' || character == '_';
			});
	}

	bool IsChildPath(const std::filesystem::path& path) {

		if (path.empty() || path.is_absolute()) {
			return false;
		}
		for (const auto& part : path) {
			if (part == "..") {
				return false;
			}
		}
		return true;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	nlohmann::json LoadJson(const std::filesystem::path& path) {

		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) {
			return {};
		}
		return nlohmann::json::parse(file, nullptr, false);
	}

	void AddIssue(Engine::PackageResolveResult& result,
		std::string packageName, std::string detail) {

		result.issues.emplace_back(std::move(packageName), std::move(detail));
	}

	std::optional<DependencySpec> ParseDependency(std::string_view name,
		const nlohmann::json& value, const std::filesystem::path& relativeBase,
		Engine::PackageResolveResult& result) {

		DependencySpec spec{};
		spec.name = name;
		spec.source = "embedded";
		spec.relativeBase = relativeBase;

		if (!IsValidPackageName(name)) {
			AddIssue(result, spec.name, "invalid package name");
			return std::nullopt;
		}

		if (value.is_string()) {

			const std::string text = value.get<std::string>();
			constexpr std::string_view kFilePrefix = "file:";
			if (text.starts_with(kFilePrefix)) {
				spec.source = "local";
				spec.path = Engine::Algorithm::PathFromUTF8(text.substr(kFilePrefix.size()));
			} else {
				spec.version = text;
			}
		} else if (value.is_object()) {

			spec.version = value.value("version", std::string{});
			spec.source = value.value("source", std::string("embedded"));
			spec.path = Engine::Algorithm::PathFromUTF8(value.value("path", std::string{}));
		} else {

			AddIssue(result, spec.name, "dependency must be a string or object");
			return std::nullopt;
		}

		if (spec.source != "embedded" && spec.source != "local") {
			AddIssue(result, spec.name, "unsupported package source: " + spec.source);
			return std::nullopt;
		}
		if (spec.source == "local" && !IsChildPath(spec.path)) {
			AddIssue(result, spec.name, "local package path must stay inside its dependency root");
			return std::nullopt;
		}
		return spec;
	}

	std::filesystem::path ResolvePackageRoot(const DependencySpec& spec,
		const std::filesystem::path& packagesRoot) {

		if (spec.source == "local") {
			return NormalizePath(spec.relativeBase / spec.path);
		}
		return NormalizePath(packagesRoot / Engine::Algorithm::PathFromUTF8(spec.name));
	}

	uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {

		const auto* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= kFnvPrime;
		}
		return hash;
	}

	uint64_t ComputePackageHash(const std::filesystem::path& root) {

		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(
			root, std::filesystem::directory_options::skip_permission_denied, ec);
			it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {

			if (ec) {
				ec.clear();
				continue;
			}
			if (it->is_regular_file(ec)) {
				files.emplace_back(it->path());
			}
		}
		std::sort(files.begin(), files.end(), [&root](const auto& lhs, const auto& rhs) {
			return lhs.lexically_relative(root).generic_wstring() <
				rhs.lexically_relative(root).generic_wstring();
			});

		uint64_t hash = kFnvOffset;
		std::array<char, 64 * 1024> buffer{};
		for (const std::filesystem::path& path : files) {

			const std::string relative = Engine::Algorithm::PathToUTF8(path.lexically_relative(root));
			hash = HashBytes(hash, relative.data(), relative.size());

			std::ifstream file(path, std::ios::binary);
			while (file) {
				file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				hash = HashBytes(hash, buffer.data(), static_cast<size_t>(file.gcount()));
			}
		}
		return hash;
	}

	std::string ToHex(uint64_t value) {

		std::ostringstream stream;
		stream << std::hex << std::setfill('0') << std::setw(16) << value;
		return stream.str();
	}

	bool SaveLockFile(const std::filesystem::path& path,
		const std::filesystem::path& projectRoot,
		const std::vector<Engine::ResolvedPackage>& packages) {

		nlohmann::json dependencies = nlohmann::json::object();
		for (const Engine::ResolvedPackage& package : packages) {

			dependencies[package.name] = {
				{ "version", package.version },
				{ "source", package.source },
				{ "path", Engine::Algorithm::PathToUTF8(package.root.lexically_relative(projectRoot)) },
				{ "contentHash", ToHex(package.contentHash) },
			};
		}
		const nlohmann::json lock = {
			{ "schemaVersion", kPackageLockSchemaVersion },
			{ "dependencies", std::move(dependencies) },
		};
		const std::string serialized = lock.dump(2) + '\n';

		std::ifstream currentFile(path, std::ios::binary);
		const std::string current((std::istreambuf_iterator<char>(currentFile)),
			std::istreambuf_iterator<char>());
		if (current == serialized) {
			return true;
		}

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}
		file << serialized;
		return file.good();
	}
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

		const DependencySpec& spec = pending[index];
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

		resolvedByName.emplace(spec.name, result.packages.size());
		result.packages.emplace_back(
			spec.name, actualVersion, spec.source, root, ComputePackageHash(root));

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
	if (result.Succeeded() &&
		!SaveLockFile(packagesRoot / "packages-lock.json", projectRoot, result.packages)) {
		AddIssue(result, {}, "failed to write Packages/packages-lock.json");
	}
	return result;
}
