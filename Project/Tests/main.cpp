//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Runtime/Packages/PackageResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

	struct TestEnableableComponent {

		static constexpr bool kEnableable = true;

		int32_t value = 0;
	};

	struct TestBufferElement {

		static constexpr Engine::ComponentStorageKind kStorageKind =
			Engine::ComponentStorageKind::Buffer;

		int32_t value = 0;
	};

	struct TestBlobRoot {

		uint32_t id = 0;
		Engine::BlobArray<int32_t> values{};
	};

	void to_json(nlohmann::json& out, const TestEnableableComponent& component) {

		out = component.value;
	}

	void from_json(const nlohmann::json& in, TestEnableableComponent& component) {

		component.value = in.get<int32_t>();
	}

	void to_json(nlohmann::json& out, const TestBufferElement& element) {

		out = element.value;
	}

	void from_json(const nlohmann::json& in, TestBufferElement& element) {

		element.value = in.get<int32_t>();
	}

	void RegisterTestComponents() {

		static const bool registered = [] {

			Engine::ComponentTypeRegistry& registry =
				Engine::ComponentTypeRegistry::GetInstance();
			registry.Register<TestEnableableComponent>(
				registry.GetComponentTypeCount(), "TestEnableable");
			registry.Register<TestBufferElement>(
				registry.GetComponentTypeCount(), "TestBuffer");
			return true;
			}();
		(void)registered;
	}

	bool TestAssetGUIDRoundTrip() {

		constexpr std::string_view source = "d0d59331ff0a4eb589ea7801cb52f208";
		const std::optional<Engine::AssetGUID> parsed = Engine::TryParseAssetGUID32Hex(source);
		return parsed && Engine::ToString(*parsed) == source;
	}

	bool TestBuiltinShaderSources() {

		constexpr std::array<const char*, 10> references = {
			Engine::BuiltinShaderSource::Skybox::VS,
			Engine::BuiltinShaderSource::Skybox::PS,
			Engine::BuiltinShaderSource::Line::GeometryVS,
			Engine::BuiltinShaderSource::Line::GeometryGS,
			Engine::BuiltinShaderSource::Line::GeometryPS,
			Engine::BuiltinShaderSource::Line::AnalyticGridVS,
			Engine::BuiltinShaderSource::Line::AnalyticGridPS,
			Engine::BuiltinShaderSource::Editor::PickMeshInstanceCS,
			Engine::BuiltinShaderSource::Editor::SceneOverlaySpriteVS,
			Engine::BuiltinShaderSource::Editor::SceneOverlaySpritePS,
		};

		for (const char* reference : references) {

			if (!Engine::TryParseAssetGUID32Hex(reference)) {
				return false;
			}
			const std::filesystem::path path = Engine::ShaderSourcePath::Resolve(reference);
			std::error_code ec;
			if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec) {
				return false;
			}
		}
		return Engine::ShaderSourcePath::Resolve("b2995658d93cd4ab").empty();
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

	bool TestECSChunkStorage() {

		Engine::ECSWorld world;
		world.ResetFrameStatistics();

		std::vector<Engine::Entity> entities;
		entities.reserve(160);
		for (uint32_t i = 0; i < 160; ++i) {

			const std::string name = "ChunkEntity_" + std::to_string(i);
			entities.emplace_back(Engine::SceneAuthoring::CreateGameObject(world, name));
		}

		const Engine::ECSWorldStatistics created = world.GetStatistics();
		if (created.structuralMigrationCount != 0 ||
			created.allocatedChunkCount < 2 ||
			created.allocatedChunkBytes !=
			static_cast<uint64_t>(created.allocatedChunkCount) * Engine::kChunkBytes ||
			created.allocatedChunkBytes < created.payloadBytes) {
			return false;
		}

		for (uint32_t i = 0; i < entities.size(); i += 2) {
			world.DestroyEntity(entities[i]);
		}
		world.FlushPendingDestroyEntities();

		for (uint32_t i = 1; i < entities.size(); i += 2) {
			if (!world.IsAlive(entities[i]) ||
				world.GetComponent<Engine::NameComponent>(entities[i]).name !=
				"ChunkEntity_" + std::to_string(i)) {
				return false;
			}
		}

		for (uint32_t i = 1; i < entities.size(); i += 2) {
			world.DestroyEntity(entities[i]);
		}
		world.FlushPendingDestroyEntities();

		const Engine::ECSWorldStatistics destroyed = world.GetStatistics();
		return destroyed.aliveEntityCount == 0 &&
			destroyed.allocatedChunkCount == 0 &&
			destroyed.allocatedChunkBytes == 0 &&
			destroyed.payloadBytes == 0;
	}

	bool TestECSExternalStorage() {

		struct TestBufferTag;
		Engine::RuntimeBufferPool<int32_t, TestBufferTag> buffers;
		const std::array<int32_t, 3> source = { 1, 2, 3 };
		const auto first = buffers.Create(source);
		if (!buffers.IsAlive(first) || buffers.Get(first).size() != source.size()) {
			return false;
		}
		if (!buffers.Release(first) || buffers.IsAlive(first)) {
			return false;
		}

		const auto second = buffers.Create(source);
		if (first.index != second.index || first.generation == second.generation ||
			!buffers.Get(first).empty()) {
			return false;
		}

		Engine::BlobStore blobs;
		const std::array<std::byte, 4> blobData = {
			std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
		};
		const Engine::BlobStore::Handle blobA = blobs.Acquire(blobData);
		const Engine::BlobStore::Handle blobB = blobs.Acquire(blobData);
		if (blobA != blobB || blobs.GetReferenceCount(blobA) != 2 ||
			blobs.Get(blobA).size() != blobData.size()) {
			return false;
		}
		if (!blobs.Release(blobA) || !blobs.IsAlive(blobB) ||
			blobs.GetReferenceCount(blobB) != 1) {
			return false;
		}
		return blobs.Release(blobB) && !blobs.IsAlive(blobB);
	}

	bool TestECSRuntimeData() {

		RegisterTestComponents();
		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const Engine::Entity entity = world.CreateEntity();

		// Enableable Componentは構造変更せずクエリへの参加だけを切り替える
		TestEnableableComponent& enableable =
			world.AddComponent<TestEnableableComponent>(entity);
		enableable.value = 42;
		uint32_t visibleCount = 0;
		world.ForEach<TestEnableableComponent>(
			[&](const Engine::Entity&, TestEnableableComponent&) {
				++visibleCount;
			});
		if (visibleCount != 1) {
			return false;
		}
		world.SetComponentEnabled<TestEnableableComponent>(entity, false);
		world.ForEach<TestEnableableComponent>(
			[&](const Engine::Entity&, TestEnableableComponent&) {
				++visibleCount;
			});
		if (visibleCount != 1 ||
			world.GetComponent<TestEnableableComponent>(entity).value != 42) {
			return false;
		}

		// 既定のチャンク内容量を超えた後も構造変更でBufferの所有権を維持する
		Engine::DynamicBuffer<TestBufferElement> buffer =
			world.AddBuffer<TestBufferElement>(entity);
		const uint32_t inlineCapacity = buffer.GetCapacity();
		for (uint32_t i = 0; i < inlineCapacity + 8; ++i) {
			buffer.Add(TestBufferElement{ static_cast<int32_t>(i) });
		}
		world.AddComponent<Engine::NameComponent>(entity).name = "BufferOwner";
		buffer = world.GetBuffer<TestBufferElement>(entity);
		if (buffer.GetSize() != inlineCapacity + 8 ||
			buffer[inlineCapacity + 7].value != static_cast<int32_t>(inlineCapacity + 7)) {
			return false;
		}

		// C# ABIと同じ型消去経路でもサイズ検証後に読み書きできる
		const uint32_t bufferTypeID =
			Engine::ComponentTypeRegistry::GetInstance().GetID<TestBufferElement>();
		Engine::UntypedDynamicBuffer untyped =
			world.TryGetUntypedBuffer(entity, bufferTypeID);
		const std::array<TestBufferElement, 3> replacement = {
			TestBufferElement{ 13 },
			TestBufferElement{ 17 },
			TestBufferElement{ 19 },
		};
		if (!untyped.IsValid() || !untyped.IsTriviallyCopyable() ||
			untyped.GetElementSize() != sizeof(TestBufferElement) ||
			!untyped.SetData(replacement.data(),
				static_cast<uint32_t>(replacement.size()))) {
			return false;
		}
		std::array<TestBufferElement, 2> copied{};
		if (untyped.CopyTo(copied.data(),
			static_cast<uint32_t>(copied.size()), 1) != copied.size() ||
			copied[0].value != 17 || copied[1].value != 19 ||
			!untyped.RemoveAt(1) || untyped.GetSize() != 2) {
			return false;
		}

		// Blob内配列はルートからの相対位置で参照し、同一内容を共有する
		Engine::BlobStore blobs;
		Engine::BlobBuilder<TestBlobRoot> builder;
		const std::array<int32_t, 4> values = { 3, 5, 7, 11 };
		builder.GetRoot().id = 9;
		const Engine::BlobArray<int32_t> valuesReference =
			builder.AddArray<int32_t>(values);
		builder.GetRoot().values = valuesReference;
		const Engine::BlobAssetReference<TestBlobRoot> first = builder.Build(blobs);
		const Engine::BlobAssetReference<TestBlobRoot> second = builder.Build(blobs);
		const TestBlobRoot* root = blobs.TryGetObject<TestBlobRoot>(first.handle);
		if (!root || root->id != 9 || root->values.Get(root).back() != 11 ||
			first != second || blobs.GetReferenceCount(first.handle) != 2) {
			return false;
		}
		return blobs.Release(first.handle) && blobs.Release(second.handle);
	}

	bool TestNonTrivialDynamicBuffer() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Authoring);
		const Engine::Entity source = world.CreateEntity();
		world.AddComponent<Engine::ScriptComponent>(source);

		std::vector<Engine::ScriptEntry> expected;
		expected.emplace_back(Engine::MakeScriptEntry(
			"type-guid-a", "Game.PlayerController"));
		expected.back().serializedFields["speed"] = 4.5f;
		expected.emplace_back(Engine::MakeScriptEntry(
			"type-guid-b", "Game.PlayerEffects"));
		expected.back().serializedFields["enabled"] = true;
		Engine::SetScriptEntries(world, source, expected);

		// stringとJSONを持つBufferもArchetype移動後に所有権と順序を維持する
		world.AddComponent<Engine::NameComponent>(source).name = "ScriptOwner";
		const std::span<const Engine::ScriptEntry> moved =
			Engine::GetScriptEntries(
				static_cast<const Engine::ECSWorld&>(world), source);
		if (moved.size() != expected.size() ||
			moved[0].lastKnownTypeName != expected[0].lastKnownTypeName ||
			moved[0].serializedFields.value("speed", 0.0f) != 4.5f ||
			moved[1].scriptSlotID != expected[1].scriptSlotID) {
			return false;
		}

		nlohmann::json serialized;
		if (!world.SerializeComponentToJson(source, "Script", serialized) ||
			!serialized.is_array() || serialized.size() != expected.size()) {
			return false;
		}

		// JSON追加経路でも設定Componentと関連Bufferを同じ状態へ復元する
		const Engine::Entity restored = world.CreateEntity();
		world.AddComponentFromJson(restored, "Script", serialized);
		const std::span<const Engine::ScriptEntry> restoredEntries =
			Engine::GetScriptEntries(
				static_cast<const Engine::ECSWorld&>(world), restored);
		if (restoredEntries.size() != expected.size() ||
			restoredEntries[1].lastKnownTypeName !=
			expected[1].lastKnownTypeName) {
			return false;
		}

		world.RemoveComponent<Engine::ScriptComponent>(restored);
		return !world.HasBuffer<Engine::ScriptEntry>(restored);
	}
}

int main(int argc, char* argv[]) {

	if (1 < argc && std::string_view(argv[1]) == "--ecs") {
		if (!TestECSChunkStorage() || !TestECSExternalStorage() ||
			!TestECSRuntimeData() || !TestNonTrivialDynamicBuffer()) {
			std::cerr << "ECS chunk storage failed\n";
			return 10;
		}
		std::cout << "ECS chunk storage passed\n";
		return 0;
	}

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
	if (!TestBuiltinShaderSources()) {
		std::cerr << "Builtin shader source resolution failed\n";
		return 9;
	}
	if (!TestECSChunkStorage()) {
		std::cerr << "ECS chunk storage failed\n";
		return 10;
	}
	if (!TestECSExternalStorage()) {
		std::cerr << "ECS external storage failed\n";
		return 11;
	}
	if (!TestECSRuntimeData()) {
		std::cerr << "ECS runtime data failed\n";
		return 12;
	}
	if (!TestNonTrivialDynamicBuffer()) {
		std::cerr << "ECS non-trivial buffer failed\n";
		return 13;
	}
	std::cout << "NEMTests passed\n";
	return 0;
}
