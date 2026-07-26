//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Packages/PackageResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

	bool TestAssetGUIDRoundTrip() {

		constexpr std::string_view source = "d0d59331ff0a4eb589ea7801cb52f208";
		const std::optional<Engine::AssetGUID> parsed = Engine::TryParseAssetGUID32Hex(source);
		return parsed && Engine::ToString(*parsed) == source;
	}

	bool TestContentHash() {

		const std::array<uint8_t, 3> bytes = { 'a', 'b', 'c' };
		return Engine::ContentHash::SHA256(bytes) ==
			"ba7816bf8f01cfea414140de5dae2223"
			"b00361a396177a9cb410ff61f20015ad";
	}

	bool TestPackageResolver() {

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "NEMEngineTests/PackageResolver";
		std::error_code ec;
		const std::filesystem::path normalizedRoot =
			std::filesystem::weakly_canonical(root.parent_path(), ec) / root.filename();
		if (normalizedRoot.parent_path() !=
			std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), ec) /
			"NEMEngineTests") {
			return false;
		}
		std::filesystem::remove_all(normalizedRoot, ec);
		std::filesystem::create_directories(normalizedRoot / "Packages/com.nem.test", ec);
		if (ec) {
			return false;
		}

		{
			std::ofstream file(normalizedRoot / "Packages/manifest.json", std::ios::binary);
			file << R"({
  "schemaVersion": 1,
  "dependencies": {
    "com.nem.test": "1.0.0"
  }
})";
		}
		{
			std::ofstream file(normalizedRoot / "Packages/com.nem.test/package.json", std::ios::binary);
			file << R"({
  "name": "com.nem.test",
  "version": "1.0.0"
})";
		}
		{
			std::ofstream file(normalizedRoot / "Packages/com.nem.test/data.txt", std::ios::binary);
			file << "package content";
		}

		const Engine::PackageResolveResult result = Engine::PackageResolver::Resolve(
			normalizedRoot, normalizedRoot / "Packages", normalizedRoot / "Library");
		const bool passed = result.Succeeded() && result.packages.size() == 1 &&
			result.packages.front().name == "com.nem.test" &&
			result.packages.front().version == "1.0.0" &&
			result.packages.front().contentHash != 0 &&
			std::filesystem::exists(normalizedRoot / "Packages/packages-lock.json");
		std::filesystem::remove_all(normalizedRoot, ec);
		return passed;
	}

	bool TestVirtualPath() {

		Engine::RuntimePaths::Refresh();
		const std::filesystem::path gamePath =
			Engine::RuntimePaths::ResolveVirtualPath("game://Scenes/sampleScene.scene.json");
		return gamePath == (Engine::RuntimePaths::GetGameAssetsRoot() /
			"Scenes/sampleScene.scene.json").lexically_normal() &&
			Engine::RuntimePaths::ResolveVirtualPath("game://../ProjectSettings").empty();
	}

	bool TestCanonicalSceneData() {

		nlohmann::json data = nlohmann::json::object();
		data["z"] = -0.0;
		data["a"] = 1;
		const std::string serialized = Engine::JsonAdapter::SerializeCanonical(data, 2);
		if (serialized.empty() || serialized.back() != '\n' ||
			serialized.find("-0.0") != std::string::npos ||
			serialized.find("\"a\"") > serialized.find("\"z\"")) {
			return false;
		}

		Engine::PrefabInstanceData prefab{};
		prefab.entityMap.emplace_back(Engine::UUID{ 3 }, Engine::UUID{ 30 });
		prefab.entityMap.emplace_back(Engine::UUID{ 1 }, Engine::UUID{ 10 });
		prefab.modifications.push_back({ Engine::UUID{ 2 }, "Transform/localScale", 1.0f });
		prefab.modifications.push_back({ Engine::UUID{ 2 }, "Transform/localPos", 0.0f });
		const nlohmann::json prefabJson = Engine::ToJson(prefab);
		return prefabJson["EntityMap"][0]["P"] == "0000000000000001" &&
			prefabJson["Modifications"][0]["Path"] == "Transform/localPos";
	}

	bool TestJsonSemanticMerge() {

		const nlohmann::json base = {
			{ "Entities", {
				{
					{ "LocalFileID", "0000000000000001" },
					{ "Components", { { "Transform", { { "x", 0 }, { "y", 0 } } } } },
				},
				{
					{ "LocalFileID", "0000000000000002" },
					{ "Components", { { "Transform", { { "x", 0 }, { "y", 0 } } } } },
				},
			} },
		};
		nlohmann::json ours = base;
		nlohmann::json theirs = base;
		ours["Entities"][0]["Components"]["Transform"]["x"] = 10;
		theirs["Entities"][1]["Components"]["Transform"]["y"] = 20;

		const Engine::JsonMergeResult merged =
			Engine::JsonSemanticMerge::Merge(base, ours, theirs);
		if (!merged.Succeeded() ||
			merged.merged["Entities"][0]["Components"]["Transform"]["x"] != 10 ||
			merged.merged["Entities"][1]["Components"]["Transform"]["y"] != 20) {
			return false;
		}

		theirs["Entities"][0]["Components"]["Transform"]["x"] = 30;
		const Engine::JsonMergeResult conflicted =
			Engine::JsonSemanticMerge::Merge(base, ours, theirs);
		return conflicted.conflicts.size() == 1 &&
			conflicted.conflicts.front().path ==
			"/Entities/0000000000000001/Components/Transform/x";
	}

	bool TestSubScenes() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "Tests";
		if (testRoot.parent_path().lexically_normal() !=
			Engine::RuntimePaths::GetGameAssetsRoot().lexically_normal()) {
			return false;
		}

		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		const auto SaveScene = [](const std::filesystem::path& path,
			const Engine::SceneHeader& header) {

			const nlohmann::json root = {
				{ "SchemaVersion", 3 },
				{ "Header", Engine::ToJson(header) },
				{ "ExternalActors", nlohmann::json::array() },
				{ "PrefabInstances", nlohmann::json::array() },
			};
			return Engine::JsonAdapter::SaveCanonical(path, root);
			};

		Engine::AssetDatabase database;
		database.Init();

		const std::filesystem::path childPath = testRoot / "Child.scene.json";
		Engine::SceneHeader childHeader{};
		childHeader.name = "Child";
		if (!SaveScene(childPath, childHeader)) {
			return false;
		}
		const Engine::AssetID childAsset =
			database.ImportOrGet("game://Tests/Child.scene.json", Engine::AssetType::Scene);

		const std::filesystem::path rootPath = testRoot / "Root.scene.json";
		Engine::SceneHeader rootHeader{};
		rootHeader.name = "Root";
		rootHeader.subScenes.push_back({
			.slotID = Engine::UUID{ 101 },
			.slotName = "Child",
			.sceneAsset = childAsset,
			.enabled = true,
			});
		if (!SaveScene(rootPath, rootHeader)) {
			return false;
		}
		const Engine::AssetID rootAsset =
			database.ImportOrGet("game://Tests/Root.scene.json", Engine::AssetType::Scene);

		Engine::ECSWorld world;
		Engine::SceneSystem sceneSystem;
		Engine::SceneInstanceManager scenes;
		bool passed = scenes.LoadSceneTree(database, sceneSystem, world, rootAsset) &&
			scenes.GetAll().size() == 2;
		const Engine::SceneInstance* active = scenes.GetActive();
		const Engine::UUID rootInstanceID = active ? active->instanceID : Engine::UUID{};

		Engine::UUID childInstanceID{};
		if (active && !active->childScenes.empty()) {
			childInstanceID = active->childScenes.front().childInstanceID;
		}
		Engine::SceneInstance* editableRoot = active ?
			scenes.Find(active->instanceID) : nullptr;
		if (editableRoot && !editableRoot->header.subScenes.empty()) {
			editableRoot->header.subScenes.front().slotName = "RenamedChild";
			passed &= scenes.SynchronizeSubScenes(
				database, sceneSystem, world, editableRoot->instanceID);
		} else {
			passed = false;
		}
		editableRoot = scenes.GetActive() ?
			scenes.Find(scenes.GetActive()->instanceID) : nullptr;
		passed &= editableRoot && !editableRoot->childScenes.empty() &&
			editableRoot->childScenes.front().childInstanceID == childInstanceID &&
			editableRoot->childScenes.front().slotName == "RenamedChild";

		passed &= rootInstanceID && scenes.Unload(world, rootInstanceID) && scenes.GetAll().empty();

		childHeader.subScenes.push_back({
			.slotID = Engine::UUID{ 102 },
			.slotName = "Root",
			.sceneAsset = rootAsset,
			.enabled = true,
			});
		passed &= SaveScene(childPath, childHeader);
		passed &= !scenes.LoadSceneTree(database, sceneSystem, world, rootAsset);
		passed &= scenes.GetAll().empty();
		size_t aliveCount = 0;
		world.ForEachAliveEntity([&aliveCount](Engine::Entity) { ++aliveCount; });
		passed &= aliveCount == 0;

		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}

	bool TestExternalActors() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "Tests";
		std::error_code ec;
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		Engine::AssetDatabase database;
		database.Init();
		const std::filesystem::path scenePath =
			testRoot / "ExternalActors.scene.json";
		{
			const nlohmann::json emptyScene = {
				{ "SchemaVersion", 3 },
				{ "Header", Engine::ToJson(Engine::SceneHeader{}) },
				{ "ExternalActors", nlohmann::json::array() },
				{ "PrefabInstances", nlohmann::json::array() },
			};
			if (!Engine::JsonAdapter::SaveCanonical(scenePath, emptyScene)) {
				return false;
			}
		}
		const Engine::AssetID sceneAsset = database.ImportOrGet(
			"game://Tests/ExternalActors.scene.json", Engine::AssetType::Scene);

		Engine::ECSWorld sourceWorld;
		const Engine::Entity sourceEntity = sourceWorld.CreateEntity();
		Engine::SceneAuthoring::EnsureGameObjectDefaults(
			sourceWorld, sourceEntity, "ExternalActor");
		const Engine::UUID localFileID =
			sourceWorld.GetComponent<Engine::SceneObjectComponent>(
				sourceEntity).localFileID;

		Engine::SceneHeader header{};
		header.guid = sceneAsset;
		header.name = "ExternalActors";
		Engine::SceneSystem sceneSystem;
		bool passed = sceneSystem.SaveScene(
			scenePath, sourceWorld, header, database);

		const nlohmann::json savedScene = Engine::JsonAdapter::Load(scenePath);
		const std::filesystem::path actorRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" /
			Engine::ToString(sceneAsset);
		const std::filesystem::path actorPath =
			actorRoot / (Engine::ToString(localFileID) + ".actor.json");
		passed &= savedScene.value("SchemaVersion", 0) == 3 &&
			!savedScene.contains("Entities") &&
			savedScene.contains("ExternalActors") &&
			savedScene["ExternalActors"].is_array() &&
			savedScene["ExternalActors"].size() == 1 &&
			std::filesystem::exists(actorPath);

		Engine::ECSWorld loadedWorld;
		std::vector<Engine::Entity> loadedEntities;
		passed &= sceneSystem.LoadScene(scenePath, loadedWorld, &database,
			sceneAsset, Engine::UUID{ 300 }, nullptr, &loadedEntities);
		passed &= loadedEntities.size() == 1 &&
			loadedWorld.IsAlive(loadedEntities.front()) &&
			loadedWorld.GetComponent<Engine::SceneObjectComponent>(
				loadedEntities.front()).localFileID == localFileID;

		database.RebuildMeta();
		const bool actorWasImported = std::any_of(
			database.GetAssets().begin(), database.GetAssets().end(),
			[](const auto& entry) {
				return entry.second.assetPath.ends_with(".actor.json");
			});
		passed &= !actorWasImported;

		std::filesystem::remove_all(testRoot, ec);
		ec.clear();
		std::filesystem::remove_all(actorRoot, ec);
		return passed && !ec;
	}
}

int main() {

	if (!TestAssetGUIDRoundTrip()) {
		std::cerr << "AssetGUID round-trip failed\n";
		return 1;
	}
	if (!TestContentHash()) {
		std::cerr << "Content hash failed\n";
		return 2;
	}
	if (!TestPackageResolver()) {
		std::cerr << "Package resolver failed\n";
		return 3;
	}
	if (!TestVirtualPath()) {
		std::cerr << "Virtual path failed\n";
		return 4;
	}
	if (!TestCanonicalSceneData()) {
		std::cerr << "Canonical scene data failed\n";
		return 5;
	}
	if (!TestJsonSemanticMerge()) {
		std::cerr << "Semantic JSON merge failed\n";
		return 6;
	}
	if (!TestSubScenes()) {
		std::cerr << "SubScene failed\n";
		return 7;
	}
	if (!TestExternalActors()) {
		std::cerr << "ExternalActors failed\n";
		return 8;
	}
	std::cout << "NEMTests passed\n";
	return 0;
}
