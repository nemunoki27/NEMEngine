//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>

// c++
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

	int ValidateProject(const std::filesystem::path& projectPath) {

		std::error_code ec;
		std::filesystem::current_path(projectPath, ec);
		if (ec) {
			std::cerr << "Failed to set project directory\n";
			return 2;
		}

		Engine::RuntimePaths::Refresh();
		Engine::AssetDatabase database;
		if (!database.Init() || !database.RebuildMeta()) {
			std::cerr << "Failed to build AssetDatabase\n";
			return 3;
		}

		for (const Engine::PackageResolveIssue& issue : Engine::RuntimePaths::GetPackageIssues()) {
			std::cerr << issue.packageName << ": " << issue.detail << '\n';
		}
		for (const Engine::AssetDatabaseIssue& issue : database.GetIssues()) {
			std::cerr << issue.assetPath << ": " << issue.detail << '\n';
		}
		return Engine::RuntimePaths::GetPackageIssues().empty() &&
			database.GetIssues().empty() ? 0 : 4;
	}

	bool CanonicalizeSceneFile(const std::filesystem::path& path) {

		nlohmann::json root = Engine::JsonAdapter::Load(path);
		if (!root.is_object() ||
			root.value("SchemaVersion", 0u) != 3u ||
			!root.contains("Header") || !root["Header"].is_object() ||
			!root.contains("ExternalActors") || !root["ExternalActors"].is_array() ||
			!root.contains("PrefabInstances") || !root["PrefabInstances"].is_array() ||
			root.contains("Entities")) {
			return false;
		}

		if (auto header = root.find("Header");
			header != root.end() && header->is_object()) {

			auto subScenes = header->find("subScenes");
			if (subScenes != header->end() && subScenes->is_array()) {

				for (size_t index = 0; index < subScenes->size(); ++index) {

					nlohmann::json& item = (*subScenes)[index];
					if (!item.is_object() ||
						!Engine::TryParseUUID16Hex(
							item.value("slotID", std::string{}))) {
						return false;
					}
				}
			}
		}

		std::sort(root["ExternalActors"].begin(), root["ExternalActors"].end());
		for (auto& item : root["PrefabInstances"]) {

			Engine::PrefabInstanceData data{};
			if (!Engine::FromJson(item, data)) {
				return false;
			}
			item = Engine::ToJson(data);
		}
		std::sort(root["PrefabInstances"].begin(), root["PrefabInstances"].end(),
			[](const auto& lhs, const auto& rhs) {
			return lhs.value("InstanceID", std::string{}) <
				rhs.value("InstanceID", std::string{});
			});
		return Engine::JsonAdapter::SaveCanonical(path, root);
	}

	bool CanonicalizeSceneRoot(const std::filesystem::path& root, size_t& sceneCount) {

		std::error_code ec;
		for (auto it = std::filesystem::recursive_directory_iterator(
			root, std::filesystem::directory_options::skip_permission_denied, ec);
			it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {

			if (ec) {
				ec.clear();
				continue;
			}
			if (!it->is_regular_file(ec) ||
				Engine::AssetTypeResolver::GuessByPath(it->path()) != Engine::AssetType::Scene) {
				continue;
			}
			if (!CanonicalizeSceneFile(it->path())) {
				std::cerr << "Failed to canonicalize: " << it->path() << '\n';
				return false;
			}
			++sceneCount;
		}
		return true;
	}

	int CanonicalizeScenes(const std::filesystem::path& projectPath, bool includeEngine) {

		std::error_code ec;
		std::filesystem::current_path(projectPath, ec);
		if (ec) {
			std::cerr << "Failed to set project directory\n";
			return 2;
		}
		Engine::RuntimePaths::Refresh();

		size_t sceneCount = 0;
		if (!CanonicalizeSceneRoot(Engine::RuntimePaths::GetGameAssetsRoot(), sceneCount)) {
			return 5;
		}
		if (includeEngine &&
			!CanonicalizeSceneRoot(Engine::RuntimePaths::GetEngineAssetsRoot(), sceneCount)) {
			return 5;
		}
		std::cout << "Canonicalized scenes: " << sceneCount << '\n';
		return 0;
	}

	int MergeJsonFiles(const std::filesystem::path& basePath,
		const std::filesystem::path& ourPath,
		const std::filesystem::path& theirPath,
		const std::filesystem::path& outputPath) {

		const nlohmann::json base = Engine::JsonAdapter::Load(basePath);
		const nlohmann::json ours = Engine::JsonAdapter::Load(ourPath);
		const nlohmann::json theirs = Engine::JsonAdapter::Load(theirPath);
		if (base.is_null() || ours.is_null() || theirs.is_null()) {
			std::cerr << "Failed to load merge input\n";
			return 6;
		}

		const Engine::JsonMergeResult result =
			Engine::JsonSemanticMerge::Merge(base, ours, theirs);
		if (!Engine::JsonAdapter::SaveCanonical(outputPath, result.merged)) {
			std::cerr << "Failed to save merge output\n";
			return 6;
		}

		std::filesystem::path conflictPath = outputPath;
		conflictPath += L".merge-conflicts.json";
		if (result.Succeeded()) {

			std::error_code ec;
			std::filesystem::remove(conflictPath, ec);
			return 0;
		}

		nlohmann::json conflictReport = nlohmann::json::object();
		conflictReport["conflicts"] = nlohmann::json::array();
		for (const Engine::JsonMergeConflict& conflict : result.conflicts) {
			conflictReport["conflicts"].push_back({
				{ "path", conflict.path },
				{ "base", conflict.base },
				{ "ours", conflict.ours },
				{ "theirs", conflict.theirs },
				});
		}
		if (!Engine::JsonAdapter::SaveCanonical(conflictPath, conflictReport)) {
			return 6;
		}
		std::cerr << "Semantic merge conflicts: " <<
			result.conflicts.size() << '\n';
		return 7;
	}

	int VerifyCook(const std::filesystem::path& manifestPath,
		const std::filesystem::path& contentRoot) {

		const nlohmann::json manifest =
			Engine::JsonAdapter::Load(manifestPath);
		if (!manifest.is_object() ||
			manifest.value("schemaVersion", 0) != 1 ||
			!manifest.contains("files") || !manifest["files"].is_array()) {
			std::cerr << "Cook manifest is invalid\n";
			return 9;
		}

		std::error_code ec;
		const std::filesystem::path normalizedRoot =
			std::filesystem::weakly_canonical(contentRoot, ec);
		if (ec || !std::filesystem::is_directory(normalizedRoot, ec)) {
			std::cerr << "Cook root was not found\n";
			return 9;
		}

		size_t fileCount = 0;
		for (const nlohmann::json& entry : manifest["files"]) {

			const std::filesystem::path relative =
				Engine::Algorithm::PathFromUTF8(
					entry.value("path", std::string{}));
			if (relative.empty() || relative.is_absolute()) {
				return 9;
			}
			const std::filesystem::path fullPath =
				(normalizedRoot / relative).lexically_normal();
			const std::filesystem::path relativeCheck =
				fullPath.lexically_relative(normalizedRoot);
			if (relativeCheck.empty() ||
				relativeCheck.native().starts_with(L"..") ||
				!std::filesystem::is_regular_file(fullPath, ec) ||
				std::filesystem::file_size(fullPath, ec) !=
				entry.value("size", uintmax_t{ 0 }) ||
				Engine::ContentHash::FileSHA256(fullPath) !=
				entry.value("sha256", std::string{})) {

				std::cerr << "Cook verification failed: " <<
					Engine::Algorithm::PathToUTF8(relative) << '\n';
				return 10;
			}
			++fileCount;
		}
		std::cout << "Cook verified: " << fileCount << " files\n";
		return 0;
	}
}

int main(int argc, char** argv) {

	if (argc == 3 && std::string_view(argv[1]) == "--validate-project") {
		return ValidateProject(std::filesystem::path(argv[2]));
	}
	if (argc == 3 && std::string_view(argv[1]) == "--canonicalize-scenes") {
		return CanonicalizeScenes(std::filesystem::path(argv[2]), false);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--canonicalize-scenes" &&
		std::string_view(argv[3]) == "--include-engine") {
		return CanonicalizeScenes(std::filesystem::path(argv[2]), true);
	}
	if (argc == 6 && std::string_view(argv[1]) == "--merge-json") {
		return MergeJsonFiles(argv[2], argv[3], argv[4], argv[5]);
	}
	if (argc == 4 && std::string_view(argv[1]) == "--verify-cook") {
		return VerifyCook(argv[2], argv[3]);
	}

	std::cout << "NEMBuildTool --validate-project <project-directory>\n"
		"NEMBuildTool --canonicalize-scenes <project-directory> [--include-engine]\n"
		"NEMBuildTool --merge-json <base> <ours> <theirs> <output>\n"
		"NEMBuildTool --verify-cook <manifest> <content-root>\n";
	return argc == 1 ? 0 : 1;
}
