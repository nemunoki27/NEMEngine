#include "SceneDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
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

	bool ValidateExternalActor(const nlohmann::json& actor, UUID localFileID) {

		// 数値や文字列を変換する前に保存形式を確認する
		if (!localFileID || !actor.is_object() || !actor.contains("SchemaVersion") ||
			!actor["SchemaVersion"].is_number_integer() || actor["SchemaVersion"] != kExternalActorSchemaVersion ||
			!actor.contains("LocalFileID") || !actor["LocalFileID"].is_string() ||
			!actor.contains("Components") || !actor["Components"].is_object()) return false;
		const auto parsed = TryParseUUID16Hex(actor["LocalFileID"].get<std::string>());
		return parsed && *parsed == localFileID;
	}

	bool ValidateSceneFileRoot(const nlohmann::json& root) {

		if (!root.is_object()) {
			return false;
		}
		// 両形式の混在や不正な配列を見落とさない
		const bool hasExternalActors = root.contains("ExternalActors");
		const bool hasEntities = root.contains("Entities");
		if (hasExternalActors == hasEntities ||
			(hasExternalActors && !root["ExternalActors"].is_array()) ||
			(hasEntities && !root["Entities"].is_array())) {
			return false;
		}
		// 整数への変換で不正な版番号を丸めない
		return root.contains("SchemaVersion") && root["SchemaVersion"].is_number_integer() &&
			root["SchemaVersion"] == kSceneSchemaVersion &&
			root.contains("Header") && root["Header"].is_object() &&
			root.contains("PrefabInstances") && root["PrefabInstances"].is_array() &&
			hasExternalActors != hasEntities;
	}

	// 保存値を作り直さず配列内の項目を並べ替える
	void SortStoredItems(nlohmann::json& object, const char* member, const char* primary, const char* secondary = "") {

		const auto items = object.find(member);
		if (items == object.end()) return;
		std::stable_sort(items->begin(), items->end(), [primary, secondary](const auto& lhs, const auto& rhs) {
			if (!*primary) return lhs < rhs;
			const auto left = lhs.value(primary, std::string{});
			const auto right = rhs.value(primary, std::string{});
			return left != right ? left < right : *secondary &&
				lhs.value(secondary, std::string{}) < rhs.value(secondary, std::string{});
		});
	}

	// Prefabの既知の保存配列だけを整列する
	void SortPrefabItems(nlohmann::json& prefab) {

		SortStoredItems(prefab, "EntityMap", "P", "S");
		SortStoredItems(prefab, "AddedEntityMap", "P", "S");
		SortStoredItems(prefab, "Modifications", "Target", "Path");
		SortStoredItems(prefab, "AddedComponents", "Target", "Type");
		SortStoredItems(prefab, "RemovedComponents", "Target", "Type");
		SortStoredItems(prefab, "HierarchyMods", "Target");
		SortStoredItems(prefab, "RemovedEntities", "");
		SortStoredItems(prefab, "AddedEntities", "SceneLocalFileID");
		SortStoredItems(prefab, "RemovedNestedSlots", "");
		SortStoredItems(prefab, "NestedInstances", "NestedSlotID");
		if (auto nested = prefab.find("NestedInstances"); nested != prefab.end()) {
			for (auto& child : *nested) SortPrefabItems(child);
		}
	}

	bool Canonicalize(nlohmann::json& root) try {

		// 検証に失敗した文書は変更しない
		if (!ValidateSceneFileRoot(root)) return false;
		nlohmann::json sorted = root;
		std::unordered_set<UUID> entityIDs;
		if (sorted.contains("ExternalActors")) {
			for (const auto& item : sorted["ExternalActors"]) {
				if (!item.is_string()) return false;
				const auto id = TryParseUUID16Hex(item.get<std::string>());
				if (!id || !entityIDs.insert(*id).second) return false;
			}
			SortStoredItems(sorted, "ExternalActors", "");
		} else {
			for (const auto& item : sorted["Entities"]) {
				if (!item.is_object() || !item.contains("Components") || !item["Components"].is_object()) return false;
				const auto id = TryParseUUID16Hex(item.value("LocalFileID", std::string{}));
				if (!id || !entityIDs.insert(*id).second) return false;
			}
			SortStoredItems(sorted, "Entities", "LocalFileID");
		}
		if (const auto subScenes = sorted["Header"].find("subScenes"); subScenes != sorted["Header"].end()) {
			if (!subScenes->is_array()) return false;
			std::unordered_set<UUID> slots;
			for (const auto& item : *subScenes) {
				if (!item.is_object()) return false;
				const auto id = TryParseUUID16Hex(item.value("slotID", std::string{}));
				if (!id || !slots.insert(*id).second) return false;
			}
		}
		std::unordered_set<UUID> instances;
		for (auto& item : sorted["PrefabInstances"]) {
			PrefabInstanceData data;
			if (!FromJson(item, data) || !data.instanceID || !instances.insert(data.instanceID).second) return false;
			SortPrefabItems(item);
		}
		SortStoredItems(sorted, "PrefabInstances", "InstanceID");
		root = std::move(sorted);
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
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
					if (!ValidateExternalActor(actor, localFileIDs[actorIndex])) {

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
