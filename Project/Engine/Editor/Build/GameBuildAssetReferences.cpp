#include "GameBuildAssetCollector.h"
#include "GameBuildUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/Scene/Serialization/SceneStorageJournal.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <fstream>
#include <regex>
#include <sstream>

namespace Engine {

using namespace GameBuildUtility;
using BuildFileEntry = GameBuildFileEntry;

void GameBuildAssetCollector::InspectFile(const Engine::AssetMeta& meta, const std::filesystem::path& source) {

	const std::string extension = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(source.extension()));
	if (extension == ".json" || extension == ".effect" || extension == ".prefab" || extension == ".scene") {

		const nlohmann::json data = LoadJson(source);
		if (!data.is_discarded() && !data.is_null()) {
			if (meta.type == Engine::AssetType::Scene) {
				CollectExternalActors(meta, source, data);
			}
			InspectJson(data);
		}
	}
	if (extension == ".hlsl" || extension == ".hlsli") {
		CollectShaderIncludes(source);
	}
	if (meta.type == Engine::AssetType::Mesh) {
		CollectModelSidecars(source);
	}
}

void GameBuildAssetCollector::CollectExternalActors(const Engine::AssetMeta& sceneMeta,
			const std::filesystem::path& scenePath,
			const nlohmann::json& sceneData) {

	if (!sceneData.contains("ExternalActors") ||
		!sceneData["ExternalActors"].is_array()) {
		return;
	}

	const auto issues = sceneStorage_->Validate(scenePath, sceneMeta.guid);
	if (!issues.empty()) {
		for (const auto& issue : issues) {
			errors_.push_back(issue.detail + " scene=" + sceneMeta.assetPath + " path=" +
				Engine::Algorithm::PathToUTF8(issue.actorPath) + " / Projectパネルのシーンデータ検証・修復を確認してください");
		}
		return;
	}
	const std::filesystem::path actorRoot = Engine::SceneAssetStorage::ResolveActorRoot(scenePath, sceneMeta.guid);
	for (const nlohmann::json& actorID : sceneData["ExternalActors"]) {

		if (!actorID.is_string() ||
			!Engine::TryParseUUID16Hex(actorID.get<std::string>())) {
			errors_.push_back("ExternalActor IDが不正です: " +
				sceneMeta.assetPath);
			continue;
		}
		const std::filesystem::path actorPath =
			actorRoot /
			(actorID.get<std::string>() + ".actor.json");
		const std::string assetPath =
			Engine::RuntimePaths::ToAssetPath(actorPath);
		if (assetPath.empty() ||
			!std::filesystem::is_regular_file(actorPath)) {
			errors_.push_back("ExternalActorが見つかりません: " +
				Engine::Algorithm::PathToUTF8(actorPath));
			continue;
		}

		AddFile(actorPath, ToBuildDestination(assetPath));
		const nlohmann::json actor = LoadJson(actorPath);
		if (actor.is_object()) {
			InspectJson(actor);
		}
	}
}

void GameBuildAssetCollector::InspectJson(const nlohmann::json& node) {

	if (node.is_object()) {

		for (auto it = node.begin(); it != node.end(); ++it) {

			const std::string& key = it.key();
			if (it->is_string()) {

				const std::string value = it->get<std::string>();
				if (const std::optional<Engine::AssetID> parsed = Engine::TryParseAssetGUID32Hex(value)) {
					if (database_.Find(*parsed)) {
						AddAsset(*parsed);
					}
				} else if (key == "file" &&
					(StartsWith(value, "Engine/Assets/") || StartsWith(value, "GameAssets/"))) {
					AddLogicalFile(value);
				}
			}
			InspectJson(*it);
		}
	} else if (node.is_array()) {

		for (const nlohmann::json& element : node) {
			InspectJson(element);
		}
	}
}

void GameBuildAssetCollector::CollectShaderIncludes(const std::filesystem::path& shaderPath) {

	std::error_code ec;
	const std::filesystem::path normalized = std::filesystem::weakly_canonical(shaderPath, ec);
	const std::string key = Engine::Algorithm::ConvertString(
		(ec ? shaderPath.lexically_normal() : normalized).generic_wstring());
	if (!scannedShaderFiles_.insert(key).second) {
		return;
	}

	std::ifstream file(shaderPath);
	if (!file.is_open()) {
		return;
	}

	static const std::regex kIncludePattern(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
	std::string line;
	while (std::getline(file, line)) {

		std::smatch match;
		if (!std::regex_search(line, match, kIncludePattern) || match.size() < 2) {
			continue;
		}

		const std::filesystem::path includeName = Engine::Algorithm::PathFromUTF8(match[1].str());
		const std::array<std::filesystem::path, 3> candidates = {
			shaderPath.parent_path() / includeName,
			Engine::RuntimePaths::GetEngineAssetPath("Shaders") / includeName,
			Engine::RuntimePaths::GetGameRoot() / "GameAssets/Shaders" / includeName,
		};
		for (const std::filesystem::path& candidate : candidates) {

			if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
				ec.clear();
				continue;
			}
			const std::string assetPath = Engine::RuntimePaths::ToAssetPath(candidate);
			AddLogicalFile(assetPath);
			CollectShaderIncludes(candidate);
			break;
		}
	}
}

void GameBuildAssetCollector::CollectModelSidecars(const std::filesystem::path& modelPath) {

	const std::string extension = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(modelPath.extension()));
	if (extension == ".gltf") {

		const nlohmann::json data = LoadJson(modelPath);
		CollectModelUris(data, modelPath.parent_path());
	} else if (extension == ".obj") {

		CollectObjSidecars(modelPath);
	}
}

void GameBuildAssetCollector::CollectModelUris(const nlohmann::json& node, const std::filesystem::path& modelDirectory) {

	if (node.is_object()) {

		for (auto it = node.begin(); it != node.end(); ++it) {

			if (it.key() == "uri" && it->is_string()) {

				const std::string uri = it->get<std::string>();
				if (!StartsWith(uri, "data:")) {
					AddModelSidecar(modelDirectory / Engine::Algorithm::PathFromUTF8(uri));
				}
			}
			CollectModelUris(*it, modelDirectory);
		}
	} else if (node.is_array()) {

		for (const nlohmann::json& element : node) {
			CollectModelUris(element, modelDirectory);
		}
	}
}

void GameBuildAssetCollector::CollectObjSidecars(const std::filesystem::path& modelPath) {

	std::ifstream file(modelPath);
	if (!file.is_open()) {
		return;
	}

	std::string line;
	while (std::getline(file, line)) {

		std::istringstream stream(line);
		std::string command;
		stream >> command;
		if (command != "mtllib") {
			continue;
		}

		std::string relative;
		std::getline(stream >> std::ws, relative);
		const std::filesystem::path materialPath =
			modelPath.parent_path() / Engine::Algorithm::PathFromUTF8(relative);
		AddModelSidecar(materialPath);
		CollectMtlTextures(materialPath);
	}
}

void GameBuildAssetCollector::CollectMtlTextures(const std::filesystem::path& materialPath) {

	std::ifstream file(materialPath);
	if (!file.is_open()) {
		return;
	}

	std::string line;
	while (std::getline(file, line)) {

		std::istringstream stream(line);
		std::string command;
		stream >> command;
		const std::string lower = Engine::Algorithm::ToLower(command);
		if (!StartsWith(lower, "map_") && lower != "bump" && lower != "disp" && lower != "decal") {
			continue;
		}

		std::string relative;
		std::getline(stream >> std::ws, relative);
		AddModelSidecar(materialPath.parent_path() / Engine::Algorithm::PathFromUTF8(relative));
	}
}

void GameBuildAssetCollector::AddModelSidecar(const std::filesystem::path& source) {

	const std::string assetPath = Engine::RuntimePaths::ToAssetPath(source);
	if (assetPath.empty()) {
		return;
	}
	AddLogicalFile(assetPath);
}
}
