#include "PackageResolution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace Engine::PackageDetail {



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
}
