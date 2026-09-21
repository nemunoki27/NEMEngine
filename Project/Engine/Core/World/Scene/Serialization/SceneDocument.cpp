#include "SceneDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <atomic>
#include <thread>
#include <utility>
#include <unordered_set>
#include <vector>

namespace Engine::SceneDocument {

	constexpr uint32_t kExternalActorSchemaVersion = 1;

	bool ValidateSceneFileRoot(const nlohmann::json& root) {

		if (!root.is_object()) {
			return false;
		}
		const bool hasExternalActors =
			root.contains("ExternalActors") &&
			root["ExternalActors"].is_array();
		const bool hasEntities =
			root.contains("Entities") &&
			root["Entities"].is_array();
		return root.value("SchemaVersion", 0u) == kSceneSchemaVersion &&
			root.contains("Header") && root["Header"].is_object() &&
			root.contains("PrefabInstances") && root["PrefabInstances"].is_array() &&
			hasExternalActors != hasEntities;
	}

	std::filesystem::path NormalizePath(const std::filesystem::path& path) {

		std::error_code ec;
		const std::filesystem::path normalized =
			std::filesystem::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : normalized;
	}

	bool IsPathInside(const std::filesystem::path& path,
		const std::filesystem::path& root) {

		const std::filesystem::path normalizedPath = NormalizePath(path);
		const std::filesystem::path normalizedRoot = NormalizePath(root);
		auto pathIt = normalizedPath.begin();
		for (auto rootIt = normalizedRoot.begin(); rootIt != normalizedRoot.end();
			++rootIt, ++pathIt) {

			if (pathIt == normalizedPath.end() ||
				Engine::Algorithm::ToLower(pathIt->string()) !=
				Engine::Algorithm::ToLower(rootIt->string())) {
				return false;
			}
		}
		return true;
	}

	bool ShouldUseExternalActors(
		const std::filesystem::path& scenePath) {

		return Engine::RuntimePaths::GetSceneStorageMode() ==
				Engine::SceneStorageMode::ExternalActors &&
			IsPathInside(scenePath,
				Engine::RuntimePaths::GetGameAssetsRoot());
	}

	std::filesystem::path ResolveExternalActorsRoot(
		const std::filesystem::path& scenePath, Engine::AssetID sceneAsset) {

		return Engine::SceneAssetStorage::ResolveActorRoot(scenePath, sceneAsset);
	}

	std::filesystem::path MakeExternalActorPath(
		const std::filesystem::path& actorRoot, Engine::UUID localFileID) {

		return actorRoot / (Engine::ToString(localFileID) + ".actor.json");
	}

	bool LoadExternalActors(const std::filesystem::path& scenePath,
		Engine::AssetID sceneAsset, nlohmann::json& root) {

		const auto externalActors = root.find("ExternalActors");
		if (externalActors == root.end() || !externalActors->is_array()) {
			return false;
		}

		const std::filesystem::path actorRoot =
			ResolveExternalActorsRoot(scenePath, sceneAsset);
		if (actorRoot.empty()) {
			return false;
		}

		nlohmann::json entities = nlohmann::json::array();
		std::unordered_set<Engine::UUID> actorIDs;
		std::vector<Engine::UUID> localFileIDs;
		std::vector<std::filesystem::path> actorPaths;
		localFileIDs.reserve(externalActors->size());
		actorPaths.reserve(externalActors->size());
		for (const auto& actorReference : *externalActors) {

			if (!actorReference.is_string()) {
				return false;
			}
			const std::optional<Engine::UUID> localFileID =
				Engine::TryParseUUID16Hex(actorReference.get<std::string>());
			if (!localFileID || !actorIDs.insert(*localFileID).second) {
				return false;
			}

			localFileIDs.emplace_back(*localFileID);
			actorPaths.emplace_back(
				MakeExternalActorPath(actorRoot, *localFileID));
		}

		// 小さいExternalActorを固定ワーカーで並列解析し大量シーンの起動待ちを抑える
		std::vector<nlohmann::json> loadedActors(actorPaths.size());
		std::atomic_size_t nextActorIndex = 0;
		std::atomic_size_t failedActorIndex = actorPaths.size();
		const size_t workerCount = (std::min)(
			actorPaths.size(),
			static_cast<size_t>((std::max)(
				1u, std::thread::hardware_concurrency())));
		std::vector<std::thread> workers;
		workers.reserve(workerCount);
		for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {

			workers.emplace_back([&]() {
				while (failedActorIndex.load(std::memory_order_relaxed) ==
					actorPaths.size()) {

					const size_t actorIndex =
						nextActorIndex.fetch_add(1, std::memory_order_relaxed);
					if (actorPaths.size() <= actorIndex) {
						return;
					}

					nlohmann::json actor =
						Engine::JsonAdapter::Load(actorPaths[actorIndex], false);
					if (!actor.is_object() ||
						!actor.contains("SchemaVersion") || !actor["SchemaVersion"].is_number_unsigned() ||
						actor.value("SchemaVersion", 0u) != kExternalActorSchemaVersion ||
						!actor.contains("LocalFileID") || !actor["LocalFileID"].is_string() ||
						Engine::FromString16Hex(
							actor.value("LocalFileID", std::string{})) !=
							localFileIDs[actorIndex] ||
						!actor.contains("Components") ||
						!actor["Components"].is_object()) {

						size_t expected = actorPaths.size();
						failedActorIndex.compare_exchange_strong(
							expected, actorIndex, std::memory_order_relaxed);
						return;
					}
					actor.erase("SchemaVersion");
					loadedActors[actorIndex] = std::move(actor);
				}
				});
		}
		for (std::thread& worker : workers) {
			worker.join();
		}

		const size_t failedIndex =
			failedActorIndex.load(std::memory_order_relaxed);
		if (failedIndex != actorPaths.size()) {

			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
				"[SceneSystem] ExternalActorが存在しないか不正です path={}",
				Engine::Algorithm::PathToUTF8(actorPaths[failedIndex]));
			return false;
		}

		entities.get_ref<nlohmann::json::array_t&>().reserve(
			loadedActors.size());
		for (nlohmann::json& actor : loadedActors) {

			entities.push_back(std::move(actor));
		}
		root["Entities"] = std::move(entities);
		return true;
	}

	bool TryInsertLocalFileID(const nlohmann::json& node, const char* key,
		std::unordered_set<Engine::UUID>& ids) {

		if (!node.is_object()) {
			return false;
		}
		const std::optional<Engine::UUID> parsed =
			Engine::TryParseUUID16Hex(node.value(key, std::string{}));
		return parsed && ids.insert(*parsed).second;
	}

	bool ValidateSerializedLocalFileIDs(const nlohmann::json& root) {

		std::unordered_set<Engine::UUID> ids;
		if (const auto entities = root.find("Entities");
			entities != root.end() && entities->is_array()) {

			for (const auto& entity : *entities) {
				if (!TryInsertLocalFileID(entity, "LocalFileID", ids)) {
					return false;
				}
			}
		}
		if (const auto prefabs = root.find("PrefabInstances");
			prefabs != root.end() && prefabs->is_array()) {

			std::unordered_set<Engine::UUID> instanceIDs;
			for (const auto& prefab : *prefabs) {

				if (!TryInsertLocalFileID(prefab, "InstanceID", instanceIDs)) {
					return false;
				}
				if (const auto entityMap = prefab.find("EntityMap");
					entityMap != prefab.end() && entityMap->is_array()) {
					for (const auto& pair : *entityMap) {
						if (!TryInsertLocalFileID(pair, "S", ids)) {
							return false;
						}
					}
				}
				if (const auto addedEntities = prefab.find("AddedEntities");
					addedEntities != prefab.end() && addedEntities->is_array()) {
					for (const auto& added : *addedEntities) {
						if (!TryInsertLocalFileID(added, "SceneLocalFileID", ids)) {
							return false;
						}
					}
				}
			}
		}
		return true;
	}
}
