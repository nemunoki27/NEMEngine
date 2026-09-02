//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshletBuilder.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Physics/Collision/CollisionRaycast.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/Runtime/Packages/PackageResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

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
			Engine::BuiltinShaderSource::Editor::PickMeshRasterPS,
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
		Engine::SceneSaveSnapshot externalSnapshot{};
		bool passed = sceneSystem.CaptureSaveSnapshot(
			scenePath, sourceWorld, header, database,
			externalSnapshot);
		if (passed) {
			externalSnapshot.useExternalActors = true;
			passed = Engine::SceneSystem::WriteSaveSnapshot(
				std::move(externalSnapshot));
		}

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

		Engine::SceneSaveSnapshot monolithicSnapshot{};
		passed &= sceneSystem.CaptureSaveSnapshot(
			scenePath, sourceWorld, header, database,
			monolithicSnapshot);
		if (passed) {
			monolithicSnapshot.useExternalActors = false;
			passed &= Engine::SceneSystem::WriteSaveSnapshot(
				std::move(monolithicSnapshot));
		}
		const nlohmann::json monolithicScene =
			Engine::JsonAdapter::Load(scenePath);
		passed &= monolithicScene.value("SchemaVersion", 0) == 3 &&
			monolithicScene.contains("Entities") &&
			monolithicScene["Entities"].is_array() &&
			monolithicScene["Entities"].size() == 1 &&
			!monolithicScene.contains("ExternalActors") &&
			!std::filesystem::exists(actorRoot);

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

	bool TestSerializationClone() {

		RegisterTestComponents();
		Engine::ECSWorld world(
			Engine::ECSWorldKind::Authoring);
		const Engine::Entity entity =
			Engine::SceneAuthoring::CreateGameObject(
				world, "SnapshotSource");
		world.AddComponent<Engine::ScriptComponent>(
			entity);
		std::vector<Engine::ScriptEntry> entries{};
		entries.emplace_back(Engine::MakeScriptEntry(
			"snapshot-script", "Game.Snapshot"));
		entries.front().serializedFields[
			"value"] = 24;
		Engine::SetScriptEntries(
			world, entity, entries);

		TestEnableableComponent& enableable =
			world.AddComponent<
				TestEnableableComponent>(entity);
		enableable.value = 35;
		world.SetComponentEnabled<
			TestEnableableComponent>(entity, false);

		const Engine::UUID stableUUID =
			world.GetUUID(entity);
		std::unique_ptr<Engine::ECSWorld> snapshot =
			world.CloneForSerialization();
		if (!snapshot ||
			!snapshot->IsAlive(entity) ||
			snapshot->GetUUID(entity) != stableUUID ||
			snapshot->GetComponent<
				Engine::NameComponent>(entity).name !=
				"SnapshotSource" ||
			snapshot->IsComponentEnabled<
				TestEnableableComponent>(entity) ||
			snapshot->GetComponent<
				TestEnableableComponent>(entity).value != 35) {
			return false;
		}

		const std::span<const Engine::ScriptEntry>
			snapshotEntries =
			Engine::GetScriptEntries(
				static_cast<const Engine::ECSWorld&>(
					*snapshot), entity);
		if (snapshotEntries.size() != 1 ||
			snapshotEntries.front().serializedFields.
				value("value", 0) != 24) {
			return false;
		}

		world.GetComponent<
			Engine::NameComponent>(entity).name =
			"ChangedAfterSnapshot";
		entries.front().serializedFields[
			"value"] = 99;
		Engine::SetScriptEntries(
			world, entity, entries);
		snapshot.reset();

		const std::span<const Engine::ScriptEntry>
			sourceEntries =
			Engine::GetScriptEntries(
				static_cast<const Engine::ECSWorld&>(
					world), entity);
		return sourceEntries.size() == 1 &&
			sourceEntries.front().serializedFields.
				value("value", 0) == 99;
	}

	bool TestTransformDirtyHierarchy() {

		Engine::ECSWorld world{};
		const Engine::Entity parent = world.CreateEntity();
		const Engine::Entity child = world.CreateEntity();

		world.AddComponent<Engine::TransformComponent>(parent);
		world.AddComponent<Engine::HierarchyComponent>(parent);
		world.AddComponent<Engine::SceneObjectComponent>(parent);
		world.AddComponent<Engine::TransformComponent>(child);
		world.AddComponent<Engine::HierarchyComponent>(child);
		world.AddComponent<Engine::SceneObjectComponent>(child);

		auto& parentTransform = world.GetComponent<Engine::TransformComponent>(parent);
		auto& parentHierarchy = world.GetComponent<Engine::HierarchyComponent>(parent);
		auto& childTransform = world.GetComponent<Engine::TransformComponent>(child);
		auto& childHierarchy = world.GetComponent<Engine::HierarchyComponent>(child);
		auto& childSceneObject = world.GetComponent<Engine::SceneObjectComponent>(child);

		parentHierarchy.firstChild = child;
		parentHierarchy.lastChild = child;
		childHierarchy.parent = parent;
		parentTransform.localPos = Engine::Vector3(2.0f, 0.0f, 0.0f);
		childTransform.localPos = Engine::Vector3(1.0f, 0.0f, 0.0f);

		// LateUpdate前でも現在のlocal値から初回ワールド姿勢を取得できることを確認する
		Engine::ResolvedWorldTransform resolvedBeforeUpdate{};
		if (!Engine::TransformWorldUtility::ResolveWorldTransform(
			world, child, resolvedBeforeUpdate) ||
			std::abs(resolvedBeforeUpdate.matrix.GetTranslationValue().x - 3.0f) > 0.0001f) {
			return false;
		}

		Engine::TransformSystem transformSystem{};
		Engine::SystemContext context{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.LateUpdate(world, context);
		if (std::abs(childTransform.worldMatrix.GetTranslationValue().x - 3.0f) > 0.0001f) {
			return false;
		}

		// 親だけdirtyでも子へワールド変更を伝播する
		parentTransform.localPos.x = 5.0f;
		Engine::MarkTransformSubtreeDirty(world, parent);
		transformSystem.LateUpdate(world, context);
		if (std::abs(childTransform.worldMatrix.GetTranslationValue().x - 6.0f) > 0.0001f) {
			return false;
		}

		// 子だけの変更は現在の親ワールド行列から更新する
		childTransform.localPos.x = 2.0f;
		Engine::MarkTransformSubtreeDirty(world, child);
		transformSystem.LateUpdate(world, context);
		if (std::abs(childTransform.worldMatrix.GetTranslationValue().x - 7.0f) > 0.0001f) {
			return false;
		}

		// 非アクティブ中はdirtyを保持し、再有効化した時点で反映する
		childSceneObject.activeInHierarchy = false;
		parentTransform.localPos.x = 8.0f;
		Engine::MarkTransformSubtreeDirty(world, parent);
		transformSystem.LateUpdate(world, context);
		if (!childTransform.isDirty ||
			std::abs(childTransform.worldMatrix.GetTranslationValue().x - 7.0f) > 0.0001f) {
			return false;
		}
		childSceneObject.activeInHierarchy = true;
		Engine::MarkTransformSubtreeDirty(world, child);
		transformSystem.LateUpdate(world, context);
		return !childTransform.isDirty &&
			std::abs(childTransform.worldMatrix.GetTranslationValue().x - 10.0f) <= 0.0001f;
	}

	bool TestRigidbody2DRestingContact() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		auto createQuad = [&](const char* name, const Engine::Vector3& position,
			const Engine::Vector2& halfSize, bool isStatic) {

			const Engine::Entity entity =
				Engine::SceneAuthoring::CreateGameObject(world, name);
			auto& transform =
				world.GetComponent<Engine::TransformComponent>(entity);
			transform.dimension = Engine::Dimension::Type2D;
			transform.localPos = position;
			Engine::MarkTransformSubtreeDirty(world, entity);

			auto& collision =
				world.AddComponent<Engine::CollisionComponent>(entity);
			collision.isStatic = isStatic;
			collision.shape.type = Engine::ColliderShapeType::Quad2D;
			collision.shape.halfSize2D = halfSize;
			return entity;
		};

		// 隣接する床Colliderへ同時接触してもPlayerの接地座標が揺れないことを確認する
		const Engine::Entity player = createQuad(
			"Player", Engine::Vector3(0.0f, 0.0f, 0.0f),
			Engine::Vector2(8.0f, 8.0f), false);
		auto& body =
			world.AddComponent<Engine::Rigidbody2DComponent>(player);
		body.restitution = 0.0f;
		createQuad("FloorLeft", Engine::Vector3(-10.0f, 30.0f, 0.0f),
			Engine::Vector2(10.0f, 10.0f), true);
		createQuad("FloorRight", Engine::Vector3(10.0f, 30.0f, 0.0f),
			Engine::Vector2(10.0f, 10.0f), true);

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Play;
		context.fixedDeltaTime = 1.0f / 60.0f;
		Engine::PhysicsSystem physicsSystem{};
		Engine::TransformSystem transformSystem{};
		Engine::CollisionSystem collisionSystem{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.FixedUpdate(world, context);

		float minSettledY = (std::numeric_limits<float>::max)();
		float maxSettledY = (std::numeric_limits<float>::lowest)();
		for (uint32_t step = 0; step < 300; ++step) {

			physicsSystem.FixedUpdate(world, context);
			transformSystem.FixedUpdate(world, context);
			collisionSystem.FixedUpdate(world, context);
			if (180 <= step) {
				const float y = world.GetComponent<
					Engine::TransformComponent>(player).localPos.y;
				minSettledY = (std::min)(minSettledY, y);
				maxSettledY = (std::max)(maxSettledY, y);
			}
		}

		const float settledY = world.GetComponent<
			Engine::TransformComponent>(player).localPos.y;
		const bool passed =
			maxSettledY - minSettledY <= 0.0001f &&
			std::abs(body.linearVelocity.y) <= 0.0001f &&
			std::abs(settledY - 12.001f) <= 0.001f;
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}

	bool TestInactivePhysicsSystems() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Runtime);
		const Engine::Entity parent =
			Engine::SceneAuthoring::CreateGameObject(world, "Inactive3D");
		const Engine::Entity child =
			Engine::SceneAuthoring::CreateGameObject(world, "Inactive2D");

		Engine::HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, child, parent);

		auto& body3D = world.AddComponent<Engine::RigidbodyComponent>(parent);
		body3D.bodyType = Engine::RigidbodyType::Dynamic;
		body3D.accumulatedForce = Engine::Vector3(3.0f, 4.0f, 5.0f);
		auto& body2D = world.AddComponent<Engine::Rigidbody2DComponent>(child);
		body2D.bodyType = Engine::RigidbodyType::Dynamic;
		body2D.accumulatedForce = Engine::Vector2(3.0f, 4.0f);

		const Engine::Vector3 parentPosition =
			world.GetComponent<Engine::TransformComponent>(parent).localPos;
		const Engine::Vector3 childPosition =
			world.GetComponent<Engine::TransformComponent>(child).localPos;
		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, false) ||
			Engine::IsEntityActiveInHierarchy(world, parent) ||
			Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Play;
		context.fixedDeltaTime = 1.0f / 60.0f;
		Engine::PhysicsSystem physicsSystem{};
		physicsSystem.FixedUpdate(world, context);

		const auto& inactiveBody3D =
			world.GetComponent<Engine::RigidbodyComponent>(parent);
		const auto& inactiveBody2D =
			world.GetComponent<Engine::Rigidbody2DComponent>(child);
		if (world.GetComponent<Engine::TransformComponent>(parent).localPos != parentPosition ||
			world.GetComponent<Engine::TransformComponent>(child).localPos != childPosition ||
			inactiveBody3D.accumulatedForce != Engine::Vector3::AnyInit(0.0f) ||
			inactiveBody2D.accumulatedForce != Engine::Vector2::AnyInit(0.0f)) {
			return false;
		}

		if (!Engine::SceneObjectUtility::SetActiveSelf(world, parent, true) ||
			!Engine::IsEntityActiveInHierarchy(world, parent) ||
			!Engine::IsEntityActiveInHierarchy(world, child)) {
			return false;
		}
		physicsSystem.FixedUpdate(world, context);
		return world.GetComponent<Engine::TransformComponent>(parent).localPos.y < parentPosition.y &&
			childPosition.y < world.GetComponent<Engine::TransformComponent>(child).localPos.y;
	}

	bool TestEditCollisionState() {

		Engine::ECSWorld world(Engine::ECSWorldKind::Authoring);
		auto createCollider = [&](const char* name, const Engine::Vector3& position,
			Engine::Dimension dimension, Engine::ColliderShapeType shapeType) {

			const Engine::Entity entity =
				Engine::SceneAuthoring::CreateGameObject(world, name);
			auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
			transform.dimension = dimension;
			transform.localPos = position;
			Engine::MarkTransformSubtreeDirty(world, entity);

			auto& collision = world.AddComponent<Engine::CollisionComponent>(entity);
			collision.shape.type = shapeType;
			collision.shape.halfSize2D = Engine::Vector2(8.0f, 8.0f);
			collision.shape.radius = 8.0f;
			return entity;
		};

		const Engine::Entity quadA = createCollider(
			"QuadA", Engine::Vector3(0.0f, 0.0f, 0.0f),
			Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity quadB = createCollider(
			"QuadB", Engine::Vector3(4.0f, 0.0f, 0.0f),
			Engine::Dimension::Type2D, Engine::ColliderShapeType::Quad2D);
		const Engine::Entity sphereA = createCollider(
			"SphereA", Engine::Vector3(100.0f, 0.0f, 0.0f),
			Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);
		const Engine::Entity sphereB = createCollider(
			"SphereB", Engine::Vector3(104.0f, 0.0f, 0.0f),
			Engine::Dimension::Type3D, Engine::ColliderShapeType::Sphere3D);

		Engine::SystemContext context{};
		context.mode = Engine::WorldMode::Edit;
		Engine::TransformSystem transformSystem{};
		Engine::CollisionSystem collisionSystem{};
		transformSystem.OnWorldEnter(world, context);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		if (!Engine::IsCollisionColliding(world, quadA) ||
			!Engine::IsCollisionColliding(world, quadB) ||
			!Engine::IsCollisionColliding(world, sphereA) ||
			!Engine::IsCollisionColliding(world, sphereB)) {

			return false;
		}

		auto& quadBTransform = world.GetComponent<Engine::TransformComponent>(quadB);
		quadBTransform.localPos.x = 40.0f;
		Engine::MarkTransformSubtreeDirty(world, quadB);
		transformSystem.LateUpdate(world, context);
		collisionSystem.LateUpdate(world, context);

		const bool passed =
			!Engine::IsCollisionColliding(world, quadA) &&
			!Engine::IsCollisionColliding(world, quadB) &&
			Engine::IsCollisionColliding(world, sphereA) &&
			Engine::IsCollisionColliding(world, sphereB);
		collisionSystem.OnWorldExit(world, context);
		transformSystem.OnWorldExit(world, context);
		return passed;
	}

	bool TestCapsuleCollisions() {

		auto makeCapsule = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, const Engine::Vector3& start,
			const Engine::Vector3& end, float radius) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.segmentStart = start;
			result.segmentEnd = end;
			result.radius = radius;
			return result;
		};
		auto makeRoundShape = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, float radius) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.radius = radius;
			return result;
		};
		auto makeBox = [](Engine::ColliderShapeType type,
			const Engine::Vector3& center, const Engine::Vector3& halfExtents) {

			Engine::CollisionShapeInstance result{};
			result.type = type;
			result.center = center;
			result.halfExtents = halfExtents;
			return result;
		};
		auto collidesBothWays = [](const Engine::CollisionShapeInstance& a,
			const Engine::CollisionShapeInstance& b) {

			Engine::CollisionContact contactAB{};
			Engine::CollisionContact contactBA{};
			return Engine::TestCollision(a, b, contactAB) &&
				Engine::TestCollision(b, a, contactBA) &&
				0.0f <= contactAB.penetration && 0.0f <= contactBA.penetration &&
				Engine::Vector3::Dot(contactAB.normal, contactBA.normal) < -0.99f;
		};

		const Engine::CollisionShapeInstance capsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(0.0f, -1.0f, 0.0f),
			Engine::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
		const Engine::CollisionShapeInstance circle = makeRoundShape(
			Engine::ColliderShapeType::Circle2D,
			Engine::Vector3(0.0f, 2.5f, 0.0f), 0.6f);
		const Engine::CollisionShapeInstance quad = makeBox(
			Engine::ColliderShapeType::Quad2D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(0.6f, 0.6f, 0.0f));
		const Engine::CollisionShapeInstance otherCapsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(1.5f, -1.0f, 0.0f),
			Engine::Vector3(1.5f, 1.0f, 0.0f), 0.6f);
		if (!collidesBothWays(capsule2D, circle) ||
			!collidesBothWays(capsule2D, quad) ||
			!collidesBothWays(capsule2D, otherCapsule2D)) {
			return false;
		}
		const Engine::CollisionShapeInstance crossingCapsule2D = makeCapsule(
			Engine::ColliderShapeType::Capsule2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(-2.0f, 0.0f, 0.0f),
			Engine::Vector3(2.0f, 0.0f, 0.0f), 0.5f);
		const Engine::CollisionShapeInstance centeredQuad = makeBox(
			Engine::ColliderShapeType::Quad2D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(1.0f, 1.0f, 0.0f));
		Engine::CollisionContact crossingContact2D{};
		if (!Engine::TestCollision(
			crossingCapsule2D, centeredQuad, crossingContact2D) ||
			crossingContact2D.penetration < 1.49f) {
			return false;
		}

		Engine::CollisionShapeInstance distantCircle = circle;
		distantCircle.center = Engine::Vector3(5.0f, 0.0f, 0.0f);
		Engine::CollisionContact contact{};
		if (Engine::TestCollision(capsule2D, distantCircle, contact)) {
			return false;
		}

		const Engine::CollisionShapeInstance capsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(0.0f, -1.0f, 0.0f),
			Engine::Vector3(0.0f, 1.0f, 0.0f), 1.0f);
		const Engine::CollisionShapeInstance sphere = makeRoundShape(
			Engine::ColliderShapeType::Sphere3D,
			Engine::Vector3(0.0f, 2.5f, 0.0f), 0.6f);
		const Engine::CollisionShapeInstance aabb = makeBox(
			Engine::ColliderShapeType::AABB3D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3::AnyInit(0.6f));
		Engine::CollisionShapeInstance obb = aabb;
		obb.type = Engine::ColliderShapeType::OBB3D;
		const Engine::CollisionShapeInstance otherCapsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3(1.5f, 0.0f, 0.0f),
			Engine::Vector3(1.5f, -1.0f, 0.0f),
			Engine::Vector3(1.5f, 1.0f, 0.0f), 0.6f);
		if (!collidesBothWays(capsule3D, sphere) ||
			!collidesBothWays(capsule3D, aabb) ||
			!collidesBothWays(capsule3D, obb) ||
			!collidesBothWays(capsule3D, otherCapsule3D)) {
			return false;
		}
		const Engine::CollisionShapeInstance crossingCapsule3D = makeCapsule(
			Engine::ColliderShapeType::Capsule3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3(-2.0f, 0.0f, 0.0f),
			Engine::Vector3(2.0f, 0.0f, 0.0f), 0.5f);
		const Engine::CollisionShapeInstance centeredBox = makeBox(
			Engine::ColliderShapeType::AABB3D,
			Engine::Vector3::AnyInit(0.0f),
			Engine::Vector3::AnyInit(1.0f));
		Engine::CollisionContact crossingContact3D{};
		if (!Engine::TestCollision(
			crossingCapsule3D, centeredBox, crossingContact3D) ||
			crossingContact3D.penetration < 1.49f) {
			return false;
		}

		Engine::Ray ray{};
		ray.origin = Engine::Vector3(-3.0f, 0.0f, 0.0f);
		ray.direction = Engine::Vector3(1.0f, 0.0f, 0.0f);
		float distance = 0.0f;
		Engine::Vector3 normal{};
		if (!Engine::CollisionRaycast::RayVsCapsule(
			ray, capsule3D, 10.0f, distance, normal) ||
			std::abs(distance - 2.0f) > 0.0001f || normal.x > -0.99f) {
			return false;
		}

		Engine::Ray capRay{};
		capRay.origin = Engine::Vector3(0.0f, 3.0f, 0.0f);
		capRay.direction = Engine::Vector3(0.4f, -1.0f, 0.0f).Normalize();
		float capDistance = 0.0f;
		Engine::Vector3 capNormal{};
		float capsuleDistance = 0.0f;
		Engine::Vector3 capsuleNormal{};
		if (!Engine::CollisionRaycast::RayVsSphere(
			capRay, capsule3D.segmentEnd, capsule3D.radius,
			10.0f, capDistance, capNormal) ||
			!Engine::CollisionRaycast::RayVsCapsule(
				capRay, capsule3D, 10.0f, capsuleDistance, capsuleNormal) ||
			std::abs(capsuleDistance - capDistance) > 0.0001f) {
			return false;
		}

		Engine::TransformComponent transform{};
		transform.worldMatrix = Engine::Matrix4x4::Identity();
		Engine::CollisionShape authored2D{};
		authored2D.type = Engine::ColliderShapeType::Capsule2D;
		authored2D.capsuleSize2D = Engine::Vector2(2.0f, 4.0f);
		const Engine::CollisionShapeInstance built2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), authored2D, 0, transform);
		Engine::CollisionShape inverted2D = authored2D;
		inverted2D.capsuleSize2D = Engine::Vector2(4.0f, 2.0f);
		const Engine::CollisionShapeInstance invertedBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), inverted2D, 0, transform);
		Engine::CollisionShape horizontal2D = inverted2D;
		horizontal2D.capsuleAxis = Engine::CapsuleAxis::X;
		const Engine::CollisionShapeInstance horizontalBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), horizontal2D, 0, transform);
		Engine::CollisionShape rotated2D = authored2D;
		rotated2D.offset = Engine::Vector3(0.0f, 0.0f, 5.0f);
		rotated2D.rotationDegrees = Engine::Vector3(30.0f, 45.0f, 90.0f);
		const Engine::CollisionShapeInstance rotatedBuilt2D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), rotated2D, 0, transform);
		Engine::CollisionShape authored3D{};
		authored3D.type = Engine::ColliderShapeType::Capsule3D;
		authored3D.capsuleHeight = 3.0f;
		authored3D.capsuleAxis = Engine::CapsuleAxis::Z;
		const Engine::CollisionShapeInstance built3D =
			Engine::CollisionShapeUtility::BuildShapeInstance(
				Engine::Entity::Null(), authored3D, 0, transform);
		nlohmann::json capsuleJson = authored3D;
		const Engine::CollisionShape restored3D =
			capsuleJson.get<Engine::CollisionShape>();
		return Engine::IsCollisionShape2D(built2D.type) &&
			Engine::IsCollisionShape3D(built3D.type) &&
			std::abs(built2D.radius - 1.0f) <= 0.0001f &&
			std::abs(built2D.segmentStart.y + 1.0f) <= 0.0001f &&
			std::abs(built2D.segmentEnd.y - 1.0f) <= 0.0001f &&
			std::abs(invertedBuilt2D.radius - 1.0f) <= 0.0001f &&
			std::abs(invertedBuilt2D.segmentStart.y) <= 0.0001f &&
			std::abs(invertedBuilt2D.segmentEnd.y) <= 0.0001f &&
			std::abs(horizontalBuilt2D.radius - 1.0f) <= 0.0001f &&
			std::abs(horizontalBuilt2D.segmentStart.x + 1.0f) <= 0.0001f &&
			std::abs(horizontalBuilt2D.segmentEnd.x - 1.0f) <= 0.0001f &&
			std::abs(rotatedBuilt2D.center.z) <= 0.0001f &&
			std::abs(rotatedBuilt2D.segmentStart.z) <= 0.0001f &&
			std::abs(rotatedBuilt2D.segmentEnd.z) <= 0.0001f &&
			std::abs(std::abs(rotatedBuilt2D.segmentStart.x) - 1.0f) <= 0.0001f &&
			std::abs(std::abs(rotatedBuilt2D.segmentEnd.x) - 1.0f) <= 0.0001f &&
			std::abs(built3D.segmentStart.z + 1.0f) <= 0.0001f &&
			std::abs(built3D.segmentEnd.z - 1.0f) <= 0.0001f &&
			std::abs(restored3D.capsuleHeight - authored3D.capsuleHeight) <= 0.0001f &&
			restored3D.capsuleAxis == authored3D.capsuleAxis;
	}

	bool TestMeshLODGeneration() {

		constexpr uint32_t gridSize = 32;
		Engine::ImportedMeshAsset mesh{};
		mesh.vertices.reserve(
			static_cast<size_t>(gridSize + 1) *
			static_cast<size_t>(gridSize + 1));
		for (uint32_t z = 0; z <= gridSize; ++z) {
			for (uint32_t x = 0; x <= gridSize; ++x) {

				const float u =
					static_cast<float>(x) /
					static_cast<float>(gridSize);
				const float v =
					static_cast<float>(z) /
					static_cast<float>(gridSize);
				Engine::MeshVertex vertex{};
				vertex.position =
					Engine::Vector4(u, 0.0f, v, 1.0f);
				vertex.normal =
					Engine::Vector3(0.0f, 1.0f, 0.0f);
				vertex.uv = Engine::Vector2(u, v);
				mesh.vertices.emplace_back(vertex);
			}
		}

		mesh.indices.reserve(
			static_cast<size_t>(gridSize) *
			static_cast<size_t>(gridSize) * 6);
		for (uint32_t z = 0; z < gridSize; ++z) {
			for (uint32_t x = 0; x < gridSize; ++x) {

				const uint32_t row = gridSize + 1;
				const uint32_t i0 = z * row + x;
				const uint32_t i1 = i0 + 1;
				const uint32_t i2 = i0 + row;
				const uint32_t i3 = i2 + 1;
				mesh.indices.insert(
					mesh.indices.end(),
					{ i0, i2, i1, i1, i2, i3 });
			}
		}

		const uint32_t halfIndexCount =
			static_cast<uint32_t>(mesh.indices.size() / 2);
		Engine::SubMeshDesc firstSubMesh{};
		firstSubMesh.indexCount = halfIndexCount;
		mesh.subMeshes.emplace_back(firstSubMesh);
		Engine::SubMeshDesc secondSubMesh{};
		secondSubMesh.indexOffset = halfIndexCount;
		secondSubMesh.indexCount =
			static_cast<uint32_t>(mesh.indices.size()) -
			halfIndexCount;
		mesh.subMeshes.emplace_back(secondSubMesh);

		Engine::MeshletBuilder builder{};
		builder.Build(mesh);

		uint32_t previousIndexCount =
			mesh.lods[0].indexCount;
		uint32_t previousMeshletCount =
			mesh.lods[0].meshletCount;
		const uint32_t lod0IndexCount =
			mesh.lods[0].indexCount;
		constexpr std::array<float, Engine::kMeshLODCount>
			maximumIndexRatios = {
				1.0f, 0.45f, 0.12f, 0.04f
		};
		if (previousIndexCount != gridSize * gridSize * 6 ||
			previousMeshletCount == 0) {
			return false;
		}

		for (uint32_t lodIndex = 1;
			lodIndex < Engine::kMeshLODCount;
			++lodIndex) {

			const Engine::MeshLODRange& lod =
				mesh.lods[lodIndex];
			if (lod.indexCount == 0 ||
				lod.indexCount % 3 != 0 ||
				lod.indexCount >= previousIndexCount ||
				static_cast<float>(lod.indexCount) >
				static_cast<float>(lod0IndexCount) *
				maximumIndexRatios[lodIndex] ||
				lod.meshletCount == 0 ||
				lod.meshletCount > previousMeshletCount) {
				std::cerr << "LOD" << lodIndex <<
					" indices=" << lod.indexCount <<
					" meshlets=" << lod.meshletCount <<
					" previousIndices=" << previousIndexCount <<
					" previousMeshlets=" << previousMeshletCount <<
					'\n';
				return false;
			}
			previousIndexCount = lod.indexCount;
			previousMeshletCount = lod.meshletCount;
		}

		if (mesh.lods[Engine::kMeshLODCount - 1].
			indexCount > 64u * 3u) {
			return false;
		}

		return Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
			160.0f, 80.0f, 32.0f) &&
			!Engine::GraphicsMeshLOD::ArePixelThresholdsValid(
				534.1f, 0.1f, 0.1f);
	}

	bool TestBlendStates() {

		struct ExpectedBlendState {

			Engine::BlendMode mode;
			D3D12_BLEND source;
			D3D12_BLEND destination;
			D3D12_BLEND_OP operation;
		};
		constexpr std::array expectedStates = {
			ExpectedBlendState{ Engine::BlendMode::Normal,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Add,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Subtract,
				D3D12_BLEND_SRC_ALPHA, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_REV_SUBTRACT },
			ExpectedBlendState{ Engine::BlendMode::Multiply,
				D3D12_BLEND_ZERO, D3D12_BLEND_SRC_COLOR,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Screen,
				D3D12_BLEND_INV_DEST_COLOR, D3D12_BLEND_ONE,
				D3D12_BLEND_OP_ADD },
			ExpectedBlendState{ Engine::BlendMode::Premultiplied,
				D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA,
				D3D12_BLEND_OP_ADD },
		};
		for (const ExpectedBlendState& expected : expectedStates) {

			D3D12_RENDER_TARGET_BLEND_DESC desc{};
			Engine::BlendState{}.Create(expected.mode, desc);
			if (!desc.BlendEnable ||
				desc.SrcBlend != expected.source ||
				desc.DestBlend != expected.destination ||
				desc.BlendOp != expected.operation ||
				desc.SrcBlendAlpha != D3D12_BLEND_ONE ||
				desc.DestBlendAlpha != D3D12_BLEND_INV_SRC_ALPHA ||
				desc.BlendOpAlpha != D3D12_BLEND_OP_ADD) {

				return false;
			}
		}
		return true;
	}

	bool TestMaterialParameters() {

		Engine::MaterialParameterSet parameters{};
		Engine::MaterialParameterValue color{};
		color.value = Engine::Color4(0.25f, 0.5f, 0.75f, 1.0f);
		parameters.Set(
			Engine::MaterialParameterIDs::BaseColor,
			Engine::MaterialParameterNames::BaseColor,
			Engine::MaterialParameterSemantic::BaseColor,
			color);

		const Engine::MaterialParameterValue* byID =
			parameters.Find(Engine::MaterialParameterIDs::BaseColor);
		const Engine::MaterialParameterValue* bySemantic =
			parameters.Find(Engine::MaterialParameterSemantic::BaseColor);
		const uint64_t hash = parameters.GetContentHash();
		if (!byID || !bySemantic || byID != bySemantic || hash == 0 ||
			parameters.GetContentHash() != hash) {

			return false;
		}

		Engine::MaterialParameterValue* mutableColor =
			parameters.Find(
				Engine::MaterialParameterIDs::BaseColor);
		if (!mutableColor) {
			return false;
		}
		mutableColor->value =
			Engine::Color4(1.0f, 0.5f, 0.75f, 1.0f);
		if (parameters.GetContentHash() == hash) {
			return false;
		}

		Engine::MaterialParameterValue renamed{};
		renamed.value = Engine::Vector2(2.0f, 4.0f);
		parameters.Set(
			Engine::MaterialParameterIDs::BaseColor,
			"RenamedParameter",
			Engine::MaterialParameterSemantic::None,
			renamed);
		const Engine::MaterialParameterValue* renamedValue =
			parameters.Find(
				Engine::MaterialParameterIDs::BaseColor);
		if (parameters.size() != 1 ||
			parameters.FindByName(
				Engine::MaterialParameterNames::BaseColor) != nullptr ||
			parameters.FindByName("RenamedParameter") == nullptr ||
			!renamedValue ||
			!std::holds_alternative<Engine::Vector2>(
				renamedValue->value)) {

			return false;
		}

		// Materialとサブメッシュの表面方式がJSON往復後も維持されることを確認する
		Engine::MaterialAsset material{};
		material.renderState.overridesRenderer = true;
		material.renderState.surfaceMode =
			Engine::MaterialSurfaceMode::Masked;
		Engine::MaterialAsset restoredMaterial{};
		if (!Engine::FromJson(
			Engine::ToJson(material), restoredMaterial) ||
			restoredMaterial.renderState.surfaceMode !=
				Engine::MaterialSurfaceMode::Masked) {

			return false;
		}

		Engine::SubMeshMaterial subMesh{};
		subMesh.surfaceMode = Engine::MaterialSurfaceMode::Auto;
		subMesh.sourceSurfaceMode =
			Engine::MaterialSurfaceMode::Transparent;
		subMesh.alphaCutoff = 0.37f;
		const Engine::SubMeshMaterial restoredSubMesh =
			nlohmann::json(subMesh).get<Engine::SubMeshMaterial>();
		return restoredSubMesh.surfaceMode ==
				Engine::MaterialSurfaceMode::Auto &&
			restoredSubMesh.sourceSurfaceMode ==
				Engine::MaterialSurfaceMode::Transparent &&
			std::abs(restoredSubMesh.alphaCutoff - 0.37f) < 1e-6f &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Masked) ==
				Engine::RenderPhase::Opaque &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Transparent,
				Engine::RenderPhase::Opaque) ==
				Engine::RenderPhase::Transparent &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Transparent,
				Engine::RenderPhase::ScreenUI) ==
				Engine::RenderPhase::ScreenUI &&
			Engine::ResolveMaterialRenderPhase(
				Engine::MaterialSurfaceMode::Masked,
				Engine::RenderPhase::PostProcessUI) ==
				Engine::RenderPhase::PostProcessUI;
	}

	bool TestTransformDimensionSerialization() {

		Engine::TransformComponent source{};
		source.dimension = Engine::Dimension::Type2D;

		nlohmann::json serialized{};
		Engine::to_json(serialized, source);
		Engine::TransformComponent restored{};
		Engine::from_json(serialized, restored);

		nlohmann::json legacy = serialized;
		legacy.erase("dimension");
		Engine::TransformComponent legacyRestored{};
		Engine::from_json(legacy, legacyRestored);

		return serialized.value("dimension", -1) ==
			static_cast<int>(Engine::Dimension::Type2D) &&
			restored.dimension == Engine::Dimension::Type2D &&
			legacyRestored.dimension == Engine::Dimension::Type3D;
	}

	bool TestScreenSpaceOutlineSerialization() {

		Engine::ScreenSpaceOutlineComponent source{};
		source.alphaSource =
			Engine::ScreenSpaceOutlineAlphaSource::TextureColor;
		source.uiOcclusionMode =
			Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;

		nlohmann::json serialized{};
		Engine::to_json(serialized, source);
		Engine::ScreenSpaceOutlineComponent restored{};
		Engine::from_json(serialized, restored);

		return serialized.value("alphaSource", std::string{}) ==
			"TextureColor" &&
			serialized.value("uiOcclusionMode", std::string{}) ==
				"AlwaysVisible" &&
			restored.alphaSource ==
				Engine::ScreenSpaceOutlineAlphaSource::TextureColor &&
			restored.uiOcclusionMode ==
				Engine::ScreenSpaceOutlineUIOcclusionMode::AlwaysVisible;
	}

	bool TestUTF8Path() {

		const std::string directoryName =
			Engine::Algorithm::ConvertString(L"NEMEngineTests_日本語");
		const std::filesystem::path testRoot = std::filesystem::temp_directory_path() /
			Engine::Algorithm::PathFromUTF8(directoryName);
		const std::filesystem::path texturePath = testRoot / L"normalBlock.png";

		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		ec.clear();
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}
		{
			std::ofstream texture(texturePath, std::ios::binary);
			texture << "PNG";
		}

		const std::string serializedPath = Engine::Algorithm::PathToUTF8(texturePath);
		const std::filesystem::path restoredPath =
			Engine::Algorithm::PathFromUTF8(serializedPath);
		const bool passed = std::filesystem::exists(restoredPath, ec) && !ec &&
			Engine::Algorithm::PathToUTF8(testRoot.filename()) == directoryName &&
			Engine::AssetTypeResolver::GuessByPath(restoredPath) == Engine::AssetType::Texture;
		std::filesystem::remove_all(testRoot, ec);
		return passed;
	}

	bool TestTextureImportSettings() {

		const Engine::TextureImportSettings color =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::Color);
		if (color.colorSpace != Engine::TextureColorSpace::SRGB ||
			color.filter != Engine::TextureFilterMode::Anisotropic ||
			!color.generateMipmaps || !color.alphaColorBleed) {
			return false;
		}

		const Engine::TextureImportSettings normal =
			Engine::MakeTextureImportSettings(Engine::TextureImportPreset::NormalMap);
		if (normal.colorSpace != Engine::TextureColorSpace::Linear ||
			normal.alphaColorBleed ||
			Engine::ToD3D12Filter(normal) != D3D12_FILTER_ANISOTROPIC) {
			return false;
		}

		const nlohmann::json data = {
			{ "preset", "Data" },
			{ "filter", "Point" },
			{ "addressU", "Mirror" },
			{ "maxAnisotropy", 99 },
		};
		const Engine::TextureImportSettings parsed =
			Engine::ParseTextureImportSettings(data);
		if (parsed.preset != Engine::TextureImportPreset::Data ||
			parsed.colorSpace != Engine::TextureColorSpace::Linear ||
			parsed.filter != Engine::TextureFilterMode::Point ||
			parsed.addressU != Engine::TextureAddressMode::Mirror ||
			parsed.maxAnisotropy != 16 ||
			Engine::ToD3D12AddressMode(parsed.addressU) !=
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR) {
			return false;
		}

		if (Engine::ParseTextureImportSettings(Engine::ToJson(parsed)) != parsed) {
			return false;
		}

		const Engine::TextureImportSettings automatic{};
		return Engine::ResolveTextureColorSpace(
			automatic, Engine::TextureColorSpace::SRGB) ==
				Engine::TextureColorSpace::SRGB &&
			Engine::ResolveTextureColorSpace(
				normal, Engine::TextureColorSpace::SRGB) ==
					Engine::TextureColorSpace::Linear &&
			Engine::HashTextureImportSettings(
				automatic, Engine::TextureColorSpace::SRGB) !=
				Engine::HashTextureImportSettings(
					automatic, Engine::TextureColorSpace::Linear);
	}

	bool TestShaderReflectionMerge() {

		Engine::ShaderConstantBufferVariable vertexVariable{};
		vertexVariable.name = "metallic";
		vertexVariable.parameterID =
			Engine::MaterialParameterID::FromName(
				vertexVariable.name);
		vertexVariable.semantic =
			Engine::MaterialParameterSemantic::Metallic;
		vertexVariable.used = false;

		Engine::ShaderConstantBufferInfo vertexBuffer{};
		vertexBuffer.name = "MaterialParameters";
		vertexBuffer.bindPoint = 3;
		vertexBuffer.size = 16;
		vertexBuffer.variables.emplace_back(vertexVariable);
		Engine::ShaderReflectionInfo reflection{};
		reflection.constantBuffers.emplace_back(vertexBuffer);

		Engine::ShaderConstantBufferVariable pixelVariable =
			vertexVariable;
		pixelVariable.used = true;
		Engine::ShaderConstantBufferInfo pixelBuffer =
			vertexBuffer;
		pixelBuffer.variables.clear();
		pixelBuffer.variables.emplace_back(pixelVariable);
		Engine::ShaderReflectionInfo pixelReflection{};
		pixelReflection.constantBuffers.emplace_back(pixelBuffer);

		Engine::MergeShaderReflection(
			reflection, pixelReflection);
		const Engine::ShaderConstantBufferInfo* merged =
			Engine::FindConstantBuffer(
				reflection, "MaterialParameters");
		return merged && merged->variables.size() == 1 &&
			merged->variables.front().used;
	}

	bool TestRenderFeatureRuntimeOverrides() {

		Engine::RenderFeatureRuntimeOverrides& overrides =
			Engine::RenderFeatureRuntimeOverrides::GetInstance();
		overrides.ResetAll();
		Engine::MaterialParameterValue value{};
		value.value = 0.75f;
		const Engine::UUID passID{ 101 };
		const Engine::MaterialParameterID parameterID =
			Engine::MaterialParameterID::FromName("ReflectionStrength");
		if (!overrides.SetEnabled(passID, false) ||
			!overrides.SetParameter(passID, parameterID,
				"ReflectionStrength", value)) {

			return false;
		}
		Engine::MaterialParameterValue textureValue{};
		textureValue.value = Engine::AssetID{ 1, 2 };
		const Engine::MaterialParameterID textureID =
			Engine::MaterialParameterID::FromName("gNoiseTexture");
		if (!overrides.SetParameter(passID, textureID,
			"gNoiseTexture", textureValue)) {

			return false;
		}

		const Engine::RenderFeaturePassRuntimeOverride* effect =
			overrides.Find(passID);
		const Engine::MaterialParameterValue* parameter = effect ?
			effect->parameters.Find(parameterID) : nullptr;
		const bool valid = effect && effect->enabled.has_value() &&
			!*effect->enabled && parameter &&
			std::holds_alternative<float>(parameter->value) &&
			std::get<float>(parameter->value) == 0.75f &&
			effect->textureOverrides.contains("gNoiseTexture") &&
			effect->textureOverrides.at("gNoiseTexture") ==
				Engine::AssetID{ 1, 2 };
		const bool cleared = overrides.ClearParameter(
			passID, textureID) &&
			!effect->textureOverrides.contains("gNoiseTexture") &&
			overrides.ClearParameter(passID, parameterID) &&
			overrides.ResetPass(passID) &&
			overrides.SetGroupEnabled("Selective", false) &&
			!overrides.IsGroupEnabled("Selective", true);
		overrides.ResetAll();
		return valid && cleared && overrides.Find(passID) == nullptr;
	}

	bool TestShaderPathDependencies() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() /
			"Tests" / "RenderFeatureDependencies";
		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		const std::filesystem::path sourcePath = testRoot / "reload.CS.hlsl";
		{
			std::ofstream source(sourcePath, std::ios::binary);
			source << "[numthreads(1, 1, 1)] void main() {}";
		}
		const std::filesystem::path shaderPath = testRoot / "reload.shader.json";
		const nlohmann::json shader = {
			{ "name", "ReloadTest" },
			{ "sourceShader",
				"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl" },
			{ "stages", nlohmann::json::array({ {
				{ "stage", "CS" },
				{ "file",
					"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl" },
				{ "entry", "main" },
				{ "profile", "cs_6_0" },
			} }) },
		};
		if (!Engine::JsonAdapter::SaveCanonical(shaderPath, shader)) {
			std::filesystem::remove_all(testRoot, ec);
			return false;
		}

		Engine::AssetDatabase database{};
		database.Init();
		const Engine::AssetID sourceID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/reload.CS.hlsl",
			Engine::AssetType::Shader);
		const Engine::AssetID shaderID = database.ImportOrGet(
			"GameAssets/Tests/RenderFeatureDependencies/reload.shader.json",
			Engine::AssetType::Shader);
		database.RefreshDependencies(shaderID);
		const std::vector<Engine::AssetID>& dependencies =
			database.FindDependencies(shaderID);
		const std::vector<Engine::AssetID>& referencers =
			database.FindReferencers(sourceID);
		const bool passed = sourceID && shaderID &&
			std::find(dependencies.begin(), dependencies.end(), sourceID) !=
				dependencies.end() &&
			std::find(referencers.begin(), referencers.end(), shaderID) !=
				referencers.end();
		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}

	bool TestRayTracingPipelineSerialization() {

		const nlohmann::json source = {
			{ "name", "RayTracingTest" },
			{ "variants", nlohmann::json::array({ {
				{ "kind", "Raytracing" },
				{ "shader", "4e454d4153534554da1b7b9bf8074052" },
				{ "rayGenerationExports", { "RayGen" } },
				{ "missExports", { "Miss" } },
				{ "hitGroups", nlohmann::json::array({ {
					{ "exportName", "HitGroup" },
					{ "closestHitExport", "ClosestHit" },
					{ "kind", "Triangles" },
				} }) },
				{ "staticSamplers", nlohmann::json::array({ {
					{ "shaderRegister", 0 },
				} }) },
			} }) },
		};
		Engine::RenderPipelineAsset pipeline{};
		if (!Engine::FromJson(source, pipeline) ||
			pipeline.variants.size() != 1 ||
			pipeline.variants.front().staticSamplers.size() != 1 ||
			pipeline.variants.front().staticSamplers.front().ShaderVisibility !=
				D3D12_SHADER_VISIBILITY_ALL) {

			return false;
		}

		const nlohmann::json serialized = Engine::ToJson(pipeline);
		return serialized["variants"][0]["rayGenerationExports"][0] ==
			"RayGen" &&
			serialized["variants"][0]["staticSamplers"][0]
				["shaderVisibility"] == "D3D12_SHADER_VISIBILITY_ALL";
	}

	bool TestShaderGraphCompile() {

		const std::filesystem::path generatedRoot =
			std::filesystem::current_path() /
			"Generated/Temp/ShaderGraphTests";
		std::error_code ec{};
		std::filesystem::create_directories(
			generatedRoot, ec);
		if (ec) {
			return false;
		}
		auto writeGeneratedGraph =
			[&](const Engine::ShaderGraphAsset& sourceGraph,
				std::string_view name) {

			const std::filesystem::path graphRoot =
				generatedRoot / std::string(name);
			std::filesystem::create_directories(
				graphRoot, ec);
			if (ec) {
				return false;
			}
			const std::filesystem::path surfacePath =
				graphRoot / "surface.hlsli";
			const std::filesystem::path opaquePath =
				graphRoot / "opaque.PS.hlsl";
			const std::filesystem::path transparentPath =
				graphRoot / "transparent.PS.hlsl";
			const std::filesystem::path vertexPath =
				graphRoot / "vertex.VS.hlsl";
			const std::filesystem::path meshPath =
				graphRoot / "mesh.MS.hlsl";
			const std::filesystem::path rayTracingPath =
				graphRoot / "rayTracing.RT.hlsl";
			const Engine::ShaderGraphCompileOutput generated =
				Engine::ShaderGraphCompiler::Compile(
					sourceGraph, "surface.hlsli");
			if (!generated.Succeeded()) {
				return false;
			}
			auto write = [](const std::filesystem::path& path,
				std::string_view source) {

				std::ofstream stream(
					path, std::ios::binary |
					std::ios::trunc);
				stream.write(
					source.data(),
					static_cast<std::streamsize>(
						source.size()));
				return stream.good();
			};
			if (!write(surfacePath, generated.surfaceHLSL) ||
				!write(opaquePath, generated.opaquePixelHLSL) ||
				!write(
					transparentPath,
					generated.transparentPixelHLSL)) {

				return false;
			}
			if ((!generated.vertexHLSL.empty() &&
				!write(vertexPath, generated.vertexHLSL)) ||
				(!generated.meshHLSL.empty() &&
					!write(meshPath, generated.meshHLSL)) ||
				(!generated.rayTracingHLSL.empty() &&
					!write(rayTracingPath, generated.rayTracingHLSL))) {

				return false;
			}
			return true;
		};

		Engine::ShaderGraphAsset graph =
			Engine::CreateDefaultSurfaceShaderGraph("NEMTest");
		const Engine::ShaderGraphCompileOutput output =
			Engine::ShaderGraphCompiler::Compile(
				graph, "NEMTest.surface.hlsli");
		if (!output.Succeeded() ||
			output.parameters.size() != graph.parameters.size() ||
			!graph.parameters.empty() ||
			graph.nodes.size() != 8 ||
			output.surfaceHLSL.find("EvaluateShaderGraphSurface") ==
				std::string::npos ||
			output.surfaceHLSL.find("ShaderGraphTimeConstants") ==
				std::string::npos ||
			output.opaquePixelHLSL.find("EncodeGBuffer") ==
				std::string::npos ||
			output.transparentPixelHLSL.find("EvaluateMeshSurfaceLighting") ==
				std::string::npos ||
			output.rayTracingHLSL.find("ReflectionAnyHit") ==
				std::string::npos ||
			output.rayTracingHLSL.find("ReflectionClosestHit") ==
				std::string::npos) {

			return false;
		}

		Engine::ShaderGraphAsset groupedGraph = graph;
		const Engine::UUID groupID = Engine::UUID::New();
		groupedGraph.groups.emplace_back(
			Engine::ShaderGraphGroup{
				.id = groupID,
				.name = "NoiseA",
				.position = Engine::Vector2(32.0f, 64.0f),
				.size = Engine::Vector2(320.0f, 180.0f),
			});
		groupedGraph.nodes.front().groupID = groupID;
		Engine::ShaderGraphAsset restoredGroup{};
		if (!Engine::FromJson(
			Engine::ToJson(groupedGraph), restoredGroup) ||
			restoredGroup.groups.size() != 1 ||
			restoredGroup.nodes.front().groupID != groupID) {

			return false;
		}
		if (!writeGeneratedGraph(graph, "Mesh")) {
			return false;
		}

		// Runtime KeywordはMaterial値、Static Keywordは保存時の定数へ変換する
		Engine::ShaderGraphAsset keywordGraph =
			Engine::CreateDefaultSurfaceShaderGraph("NEMKeywordTest");
		std::erase_if(keywordGraph.links,
			[&](const Engine::ShaderGraphLink& link) {
				return link.inputNode == keywordGraph.outputNode &&
					link.inputSlot == 2;
			});
		const Engine::UUID keywordID = Engine::UUID::New();
		const Engine::UUID keywordNodeID = Engine::UUID::New();
		keywordGraph.keywords.emplace_back(Engine::ShaderGraphKeyword{
			.id = keywordID,
			.name = "Runtime Feature",
			.referenceName = "RUNTIME_FEATURE",
			.defaultIndex = 1,
			.runtimeToggle = true,
			});
		keywordGraph.nodes.emplace_back(Engine::ShaderGraphNode{
			.id = keywordNodeID,
			.kind = Engine::ShaderGraphNodeKind::Keyword,
			.keywordID = keywordID,
			});
		keywordGraph.links.emplace_back(Engine::ShaderGraphLink{
			.id = Engine::UUID::New(),
			.outputNode = keywordNodeID,
			.inputNode = keywordGraph.outputNode,
			.inputSlot = 2,
			});
		const Engine::ShaderGraphCompileOutput runtimeKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(
				keywordGraph, "NEMKeywordTest.surface.hlsli");
		const std::string runtimeKeywordName =
			runtimeKeywordOutput.parameters.empty() ? std::string{} :
			runtimeKeywordOutput.parameters.front().shaderName;
		if (!runtimeKeywordOutput.Succeeded() ||
			runtimeKeywordOutput.parameters.size() != 1 ||
			runtimeKeywordOutput.surfaceHLSL.find(
				"uint " + runtimeKeywordName + ";") ==
				std::string::npos ||
			runtimeKeywordOutput.surfaceHLSL.find(
				"graphParameters." + runtimeKeywordName) == std::string::npos) {
			return false;
		}
		if (!writeGeneratedGraph(keywordGraph, "RuntimeKeyword")) {
			return false;
		}
		keywordGraph.keywords.front().runtimeToggle = false;
		const Engine::ShaderGraphCompileOutput staticKeywordOutput =
			Engine::ShaderGraphCompiler::Compile(
				keywordGraph, "NEMKeywordTest.surface.hlsli");
		if (!staticKeywordOutput.Succeeded() ||
			!staticKeywordOutput.parameters.empty() ||
			staticKeywordOutput.surfaceHLSL.find(
				"uint " + runtimeKeywordName + ";") !=
				std::string::npos ||
			staticKeywordOutput.surfaceHLSL.find("1u") == std::string::npos) {
			return false;
		}

		const Engine::ShaderGraphAsset postProcessGraph =
			Engine::CreateDefaultPostProcessShaderGraph("NEMPostProcess");
		const Engine::ShaderGraphCompileOutput postProcessOutput =
			Engine::ShaderGraphCompiler::Compile(
				postProcessGraph, "NEMPostProcess.generated.hlsli");
		Engine::ShaderGraphAsset restoredPostProcess{};
		if (!postProcessOutput.Succeeded() ||
			postProcessOutput.computeHLSL.find("[numthreads(8, 8, 1)]") ==
				std::string::npos ||
			postProcessOutput.computeHLSL.find("gSourceColor.SampleLevel") ==
				std::string::npos ||
			!Engine::FromJson(
				Engine::ToJson(postProcessGraph), restoredPostProcess) ||
			restoredPostProcess.domain !=
				Engine::ShaderGraphDomain::PostProcess) {
			return false;
		}

		const Engine::ShaderGraphAsset rayTracingGraph =
			Engine::CreateDefaultRayTracingEffectShaderGraph(
				"NEMRayTracingFeature");
		const Engine::ShaderGraphCompileOutput rayTracingOutput =
			Engine::ShaderGraphCompiler::Compile(
				rayTracingGraph, "NEMRayTracingFeature.generated.hlsli");
		Engine::ShaderGraphAsset restoredRayTracing{};
		if (!rayTracingOutput.Succeeded() ||
			rayTracingOutput.rayTracingHLSL.find(
				"RenderFeatureRayGeneration") == std::string::npos ||
			rayTracingOutput.rayTracingHLSL.find("TraceRay(") ==
				std::string::npos ||
			!Engine::FromJson(Engine::ToJson(rayTracingGraph),
				restoredRayTracing) ||
			restoredRayTracing.domain !=
				Engine::ShaderGraphDomain::RayTracingEffect ||
			!writeGeneratedGraph(rayTracingGraph,
				"RayTracingFeature")) {

			return false;
		}

		constexpr std::array targetIncludes{
			std::pair{
				Engine::ShaderGraphTarget::Primitive3D,
				"Builtin/Primitive/primitive.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Sprite,
				"Builtin/Sprite/defaultSprite.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Text,
				"Builtin/Text/defaultText.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Primitive2D,
					"Builtin/Primitive/primitive2D.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Particle,
					"Builtin/Particle/Common/particle.hlsli" },
			std::pair{
				Engine::ShaderGraphTarget::Trail,
					"Builtin/Particle/Common/particle.hlsli" },
		};
		for (const auto& [target, include] : targetIncludes) {
			const Engine::ShaderGraphAsset targetGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMTargetTest", target);
			const Engine::ShaderGraphCompileOutput targetOutput =
				Engine::ShaderGraphCompiler::Compile(
					targetGraph,
					"NEMTargetTest.surface.hlsli");
			if (!targetOutput.Succeeded() ||
				targetOutput.opaquePixelHLSL.find(include) ==
					std::string::npos ||
				((target == Engine::ShaderGraphTarget::Particle ||
					target == Engine::ShaderGraphTarget::Trail) &&
					targetOutput.opaquePixelHLSL.find(
						"PrepareParticleBlendColor") == std::string::npos) ||
				targetGraph.nodes.size() !=
					(Engine::IsShaderGraph3DTarget(target) ?
						8u : 4u)) {

				return false;
			}
			if (!writeGeneratedGraph(
				targetGraph,
				Engine::EnumAdapter<
					Engine::ShaderGraphTarget>::
				ToString(target))) {

				return false;
			}
			Engine::ShaderGraphAsset restoredTarget{};
			if (!Engine::FromJson(
				Engine::ToJson(targetGraph),
				restoredTarget) ||
				restoredTarget.target != target) {

				return false;
			}
		}

		// Sampler Stateはノード単位の静的サンプラーとして保存、登録する
		{
			Engine::ShaderGraphAsset samplerGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMSamplerTest");
			std::erase_if(
				samplerGraph.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == samplerGraph.outputNode &&
						link.inputSlot == 0;
				});

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter = Engine::UUID::New();
			samplerGraph.parameters.emplace_back(
				Engine::ShaderGraphParameter{
					.id = textureParameter,
					.name = "SamplerTexture",
					.type = Engine::ShaderGraphValueType::Texture2D,
					.defaultValue = textureValue,
				});

			const Engine::UUID textureNode = Engine::UUID::New();
			const Engine::UUID uvNode = Engine::UUID::New();
			const Engine::UUID samplerNode = Engine::UUID::New();
			const Engine::UUID sampleNode = Engine::UUID::New();
			Engine::ShaderGraphNode sampler{
				.id = samplerNode,
				.kind = Engine::ShaderGraphNodeKind::SamplerState,
			};
			sampler.sampler.filter =
				D3D12_FILTER_ANISOTROPIC;
			sampler.sampler.addressU =
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.sampler.addressV =
				D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
			sampler.sampler.maxAnisotropy = 8;
			samplerGraph.nodes.insert(
				samplerGraph.nodes.end(), {
					Engine::ShaderGraphNode{
						.id = textureNode,
						.kind = Engine::ShaderGraphNodeKind::Parameter,
						.parameterID = textureParameter,
						.valueType = Engine::ShaderGraphValueType::Texture2D,
					},
					Engine::ShaderGraphNode{
						.id = uvNode,
						.kind = Engine::ShaderGraphNodeKind::UV,
					},
					sampler,
					Engine::ShaderGraphNode{
						.id = sampleNode,
						.kind = Engine::ShaderGraphNodeKind::TextureSample,
					},
				});
			const auto addSamplerLink =
				[&](Engine::UUID source,
					Engine::UUID destination,
					uint32_t destinationSlot) {

					samplerGraph.links.emplace_back(
						Engine::ShaderGraphLink{
							.id = Engine::UUID::New(),
							.outputNode = source,
							.inputNode = destination,
							.inputSlot = destinationSlot,
						});
				};
			addSamplerLink(textureNode, sampleNode, 0);
			addSamplerLink(uvNode, sampleNode, 1);
			addSamplerLink(samplerNode, sampleNode, 2);
			addSamplerLink(sampleNode, samplerGraph.outputNode, 0);

			const Engine::ShaderGraphCompileOutput samplerOutput =
				Engine::ShaderGraphCompiler::Compile(
					samplerGraph,
					"NEMSamplerTest.surface.hlsli");
			if (!samplerOutput.Succeeded() ||
				samplerOutput.samplers.size() != 1 ||
				samplerOutput.samplers.front().node != samplerNode ||
				samplerOutput.samplers.front().shaderRegister != 1 ||
				samplerOutput.samplers.front().settings.filter !=
					D3D12_FILTER_ANISOTROPIC ||
				samplerOutput.surfaceHLSL.find(
					"register(s1)") == std::string::npos ||
				samplerOutput.surfaceHLSL.find(
					samplerOutput.samplers.front().shaderName) ==
					std::string::npos) {

				return false;
			}
			if (!writeGeneratedGraph(
				samplerGraph, "Sampler")) {

				return false;
			}

			Engine::ShaderGraphAsset restoredSampler{};
			if (!Engine::FromJson(
				Engine::ToJson(samplerGraph), restoredSampler)) {

				return false;
			}
			const auto restoredSamplerNode = std::ranges::find_if(
				restoredSampler.nodes,
				[&](const Engine::ShaderGraphNode& node) {
					return node.id == samplerNode;
				});
			if (restoredSamplerNode == restoredSampler.nodes.end() ||
				restoredSamplerNode->sampler.filter !=
					D3D12_FILTER_ANISOTROPIC ||
				restoredSamplerNode->sampler.addressU !=
					D3D12_TEXTURE_ADDRESS_MODE_CLAMP ||
				restoredSamplerNode->sampler.addressV !=
					D3D12_TEXTURE_ADDRESS_MODE_MIRROR ||
				restoredSamplerNode->sampler.maxAnisotropy != 8) {

				return false;
			}
		}

		constexpr std::array vertexTargets{
			Engine::ShaderGraphTarget::Mesh,
			Engine::ShaderGraphTarget::Primitive3D,
			Engine::ShaderGraphTarget::Primitive2D,
		};
		for (const Engine::ShaderGraphTarget target :
			vertexTargets) {

			Engine::ShaderGraphAsset vertexGraph =
				Engine::CreateDefaultSurfaceShaderGraph(
					"NEMVertexTargetTest", target);
			const Engine::UUID vertexOutput =
				Engine::UUID::New();
			vertexGraph.nodes.emplace_back(
				Engine::ShaderGraphNode{
					.id = vertexOutput,
					.kind = Engine::ShaderGraphNodeKind::VertexOutput,
				});
			vertexGraph.vertexOutputNode = vertexOutput;

			Engine::MaterialParameterValue textureValue{};
			textureValue.value = Engine::AssetID{};
			const Engine::UUID textureParameter =
				Engine::UUID::New();
			vertexGraph.parameters.emplace_back(
				Engine::ShaderGraphParameter{
					.id = textureParameter,
					.name = "DisplacementTexture",
					.type = Engine::ShaderGraphValueType::Texture2D,
					.defaultValue = textureValue,
				});
			const Engine::UUID textureNode = Engine::UUID::New();
			const Engine::UUID uvNode = Engine::UUID::New();
			const Engine::UUID timeNode = Engine::UUID::New();
			const Engine::UUID uvAddNode = Engine::UUID::New();
			const Engine::UUID sampleNode = Engine::UUID::New();
			const Engine::UUID positionNode = Engine::UUID::New();
			const Engine::UUID positionAddNode = Engine::UUID::New();
			vertexGraph.nodes.insert(
				vertexGraph.nodes.end(), {
					Engine::ShaderGraphNode{
						.id = textureNode,
						.kind = Engine::ShaderGraphNodeKind::Parameter,
						.parameterID = textureParameter,
						.valueType = Engine::ShaderGraphValueType::Texture2D,
					},
					Engine::ShaderGraphNode{
						.id = uvNode,
						.kind = Engine::ShaderGraphNodeKind::UV,
					},
					Engine::ShaderGraphNode{
						.id = timeNode,
						.kind = Engine::ShaderGraphNodeKind::Time,
					},
					Engine::ShaderGraphNode{
						.id = uvAddNode,
						.kind = Engine::ShaderGraphNodeKind::Add,
					},
					Engine::ShaderGraphNode{
						.id = sampleNode,
						.kind = Engine::ShaderGraphNodeKind::TextureSample,
					},
					Engine::ShaderGraphNode{
						.id = positionNode,
						.kind = Engine::ShaderGraphNodeKind::ObjectPosition,
					},
					Engine::ShaderGraphNode{
						.id = positionAddNode,
						.kind = Engine::ShaderGraphNodeKind::Add,
					},
				});
			const auto addVertexLink =
				[&](Engine::UUID source, uint32_t sourceSlot,
					Engine::UUID destination, uint32_t destinationSlot) {

				vertexGraph.links.emplace_back(
					Engine::ShaderGraphLink{
						.id = Engine::UUID::New(),
						.outputNode = source,
						.outputSlot = sourceSlot,
						.inputNode = destination,
						.inputSlot = destinationSlot,
					});
			};
			addVertexLink(uvNode, 0, uvAddNode, 0);
			addVertexLink(timeNode, 0, uvAddNode, 1);
			addVertexLink(textureNode, 0, sampleNode, 0);
			addVertexLink(uvAddNode, 0, sampleNode, 1);
			addVertexLink(positionNode, 0, positionAddNode, 0);
			addVertexLink(sampleNode, 2, positionAddNode, 1);
			addVertexLink(positionAddNode, 0, vertexOutput, 0);
			const Engine::ShaderGraphCompileOutput vertexOutputResult =
				Engine::ShaderGraphCompiler::Compile(
					vertexGraph,
					"NEMVertexTargetTest.surface.hlsli");
			const bool expectsMeshShader =
				target != Engine::ShaderGraphTarget::Primitive2D;
			const std::string expectedFunction =
				target == Engine::ShaderGraphTarget::Mesh ?
					"EvaluateShaderGraphVertex" :
					(target == Engine::ShaderGraphTarget::Primitive3D ?
						"EvaluatePrimitiveShaderGraphVertex" :
						"EvaluatePrimitive2DShaderGraphVertex");
			if (!Engine::SupportsShaderGraphVertexOutput(target) ||
				!vertexOutputResult.Succeeded() ||
				vertexOutputResult.vertexHLSL.find(
					expectedFunction) == std::string::npos ||
				(expectsMeshShader !=
					!vertexOutputResult.meshHLSL.empty()) ||
				!writeGeneratedGraph(
					vertexGraph,
					std::string("Vertex") +
						std::string(Engine::EnumAdapter<
							Engine::ShaderGraphTarget>::
							ToString(target)))) {

				return false;
			}
		}

		constexpr std::array additionalNodeKinds{
			Engine::ShaderGraphNodeKind::Subtract,
			Engine::ShaderGraphNodeKind::Divide,
			Engine::ShaderGraphNodeKind::Power,
			Engine::ShaderGraphNodeKind::Sine,
			Engine::ShaderGraphNodeKind::Time,
			Engine::ShaderGraphNodeKind::Remap,
			Engine::ShaderGraphNodeKind::TilingAndOffset,
			Engine::ShaderGraphNodeKind::PolarCoordinates,
			Engine::ShaderGraphNodeKind::Split,
			Engine::ShaderGraphNodeKind::Combine,
		};
		for (const Engine::ShaderGraphNodeKind kind :
			additionalNodeKinds) {

			Engine::ShaderGraphAsset nodeGraph =
				Engine::CreateDefaultSurfaceShaderGraph("NEMNodeTest");
			for (auto it = nodeGraph.links.begin();
				it != nodeGraph.links.end();) {

				if (it->inputNode == nodeGraph.outputNode &&
					it->inputSlot == 0) {

					it = nodeGraph.links.erase(it);
					continue;
				}
				++it;
			}

			const Engine::UUID nodeID = Engine::UUID::New();
			nodeGraph.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = nodeID,
				.kind = kind,
				.previewExpanded = false,
				});
			nodeGraph.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = nodeID,
				.inputNode = nodeGraph.outputNode,
				.inputSlot = 0,
				});

			const Engine::ShaderGraphCompileOutput nodeOutput =
				Engine::ShaderGraphCompiler::Compile(
					nodeGraph, "NEMNodeTest.surface.hlsli");
			if (!nodeOutput.Succeeded()) {
				return false;
			}

			Engine::ShaderGraphAsset restored{};
			if (!Engine::FromJson(Engine::ToJson(nodeGraph), restored)) {
				return false;
			}
			const Engine::ShaderGraphNode& restoredNode =
				restored.nodes.back();
			if (restoredNode.kind != kind ||
				restoredNode.previewExpanded) {

				return false;
			}
		}

		constexpr std::array timeExpressions{
			"(shaderGraphTime).xxxx",
			"(sin(shaderGraphTime)).xxxx",
			"(cos(shaderGraphTime)).xxxx",
			"(shaderGraphDeltaTime).xxxx",
			"(shaderGraphSmoothDeltaTime).xxxx",
		};
		for (uint32_t outputSlot = 0;
			outputSlot < timeExpressions.size();
			++outputSlot) {

			Engine::ShaderGraphAsset timeGraph =
				Engine::CreateDefaultSurfaceShaderGraph("NEMTimeTest");
			std::erase_if(
				timeGraph.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == timeGraph.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID timeNodeID = Engine::UUID::New();
			timeGraph.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = timeNodeID,
				.kind = Engine::ShaderGraphNodeKind::Time,
				.previewExpanded = false,
				});
			timeGraph.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = timeNodeID,
				.outputSlot = outputSlot,
				.inputNode = timeGraph.outputNode,
				.inputSlot = 0,
				});
			const Engine::ShaderGraphCompileOutput timeOutput =
				Engine::ShaderGraphCompiler::Compile(
					timeGraph, "NEMTimeTest.surface.hlsli");
			if (!timeOutput.Succeeded() ||
				timeOutput.surfaceHLSL.find(
					timeExpressions[outputSlot]) ==
					std::string::npos) {

				return false;
			}
		}

		// Sub Graphは公開パラメータを入力、参照先Outputを出力として展開する
		{
			Engine::ShaderGraphAsset child =
				Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraph");
			std::erase_if(child.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == child.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID parameterID = Engine::UUID::New();
			child.parameters.emplace_back(Engine::ShaderGraphParameter{
				.id = parameterID,
				.name = "Color",
				.type = Engine::ShaderGraphValueType::Color,
				.defaultValue = Engine::MaterialParameterValue{
					.value = Engine::Color4::White(),
					},
				});
			const Engine::UUID parameterNode = Engine::UUID::New();
			child.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = parameterNode,
				.kind = Engine::ShaderGraphNodeKind::Parameter,
				.parameterID = parameterID,
				.valueType = Engine::ShaderGraphValueType::Color,
				});
			child.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = parameterNode,
				.inputNode = child.outputNode,
				.inputSlot = 0,
				});

			Engine::ShaderGraphAsset parent =
				Engine::CreateDefaultSurfaceShaderGraph("NEMSubGraphParent");
			const Engine::UUID colorNode = parent.links.front().outputNode;
			std::erase_if(parent.links,
				[&](const Engine::ShaderGraphLink& link) {
					return link.inputNode == parent.outputNode &&
						link.inputSlot == 0;
				});
			const Engine::UUID subGraphNode = Engine::UUID::New();
			const Engine::AssetID subGraphID{ 10, 20 };
			parent.nodes.emplace_back(Engine::ShaderGraphNode{
				.id = subGraphNode,
				.kind = Engine::ShaderGraphNodeKind::SubGraph,
				.subGraph = subGraphID,
				});
			parent.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = colorNode,
				.inputNode = subGraphNode,
				.inputSlot = 0,
				});
			parent.links.emplace_back(Engine::ShaderGraphLink{
				.id = Engine::UUID::New(),
				.outputNode = subGraphNode,
				.outputSlot = 0,
				.inputNode = parent.outputNode,
				.inputSlot = 0,
				});
			const Engine::ShaderGraphCompileOutput subGraphOutput =
				Engine::ShaderGraphCompiler::Compile(
					parent, "NEMSubGraph.surface.hlsli",
					[&](Engine::AssetID id, Engine::ShaderGraphAsset& outGraph) {
						if (id != subGraphID) {
							return false;
						}
						outGraph = child;
						return true;
					});
			if (!subGraphOutput.Succeeded() ||
				subGraphOutput.surfaceHLSL.find("NEMSubGraph") !=
					std::string::npos) {
				return false;
			}
		}

		graph.nodes[1].id = graph.nodes[0].id;
		const Engine::ShaderGraphCompileOutput invalid =
			Engine::ShaderGraphCompiler::Compile(
				graph, "NEMTest.surface.hlsli");
		return !invalid.Succeeded() &&
			!invalid.diagnostics.empty();
	}

	bool TestRenderFeatureProfile() {

		Engine::RenderFeatureProfileAsset profile{};
		profile.name = "RenderFeatureTest";

		Engine::RenderFeaturePassSettings ao{};
		ao.id = Engine::UUID{ 11 };
		ao.name = "RTAO";
		ao.type = Engine::RenderFeaturePassType::RayTracing;
		ao.anchor = Engine::RenderFeatureAnchor::BeforeLighting;
		ao.material = Engine::AssetID{ 1, 11 };
		ao.materialPass = Engine::MaterialPassKind::RayTracing;
		ao.outputs.emplace_back(Engine::RenderFeatureOutputSettings{
			.name = "AO",
			.shaderResource = "gAmbientOcclusion",
			.format = Engine::RenderFeatureTextureFormat::R16_FLOAT,
			.widthScale = 0.5f,
			.heightScale = 0.5f,
		});

		Engine::RenderFeaturePassSettings reflection{};
		reflection.id = Engine::UUID{ 12 };
		reflection.name = "Reflection";
		reflection.type = Engine::RenderFeaturePassType::RayTracing;
		reflection.anchor = Engine::RenderFeatureAnchor::AfterLighting;
		reflection.material = Engine::AssetID{ 1, 12 };
		reflection.materialPass = Engine::MaterialPassKind::RayTracing;
		reflection.passInputs.emplace(
			"gAmbientOcclusion",
			Engine::RenderFeatureOutputReference{
				.pass = ao.id,
				.output = "AO",
			});
		reflection.outputs.emplace_back(
			Engine::RenderFeatureOutputSettings{});

		Engine::RenderFeaturePassSettings composite{};
		composite.id = Engine::UUID{ 13 };
		composite.name = "ReflectionComposite";
		composite.anchor = Engine::RenderFeatureAnchor::AfterLighting;
		composite.material = Engine::AssetID{ 1, 13 };
		composite.sourceKind = Engine::RenderFeatureSourceKind::SceneColor;
		composite.passInputs.emplace(
			"gReflectionColor",
			Engine::RenderFeatureOutputReference{
				.pass = reflection.id,
				.output = "Color",
			});
		composite.outputs.emplace_back(
			Engine::RenderFeatureOutputSettings{});
		composite.sceneColorOutput = true;
		profile.passes = { ao, reflection, composite };
		profile.hierarchy = {
			Engine::RenderFeatureHierarchyItem{
				.type = Engine::RenderFeatureHierarchyItemType::Pass,
				.id = ao.id,
			},
			Engine::RenderFeatureHierarchyItem{
				.type = Engine::RenderFeatureHierarchyItemType::Group,
				.id = Engine::UUID{ 21 },
				.name = "Reflection",
				.children = {
					Engine::RenderFeatureHierarchyItem{
						.type = Engine::RenderFeatureHierarchyItemType::Pass,
						.id = reflection.id,
					},
					Engine::RenderFeatureHierarchyItem{
						.type = Engine::RenderFeatureHierarchyItemType::Pass,
						.id = composite.id,
					},
				},
			},
		};

		const nlohmann::json data =
			Engine::RenderFeatureProfileSerializer::ToJson(profile);
		Engine::RenderFeatureProfileAsset restored =
			Engine::RenderFeatureProfileSerializer::FromJson(data);
		if (restored.name != profile.name || restored.passes.size() != 3 ||
			restored.hierarchy.size() != 2 ||
			restored.hierarchy[1].children.size() != 2 ||
			restored.passes[0].outputs[0].format !=
				Engine::RenderFeatureTextureFormat::R16_FLOAT ||
			restored.passes[1].passInputs.at("gAmbientOcclusion").pass !=
				ao.id || restored.passes[2].sourceKind !=
				Engine::RenderFeatureSourceKind::SceneColor) {

			return false;
		}

		Engine::RenderFeatureProfileRuntime runtime{};
		runtime.Rebuild(restored);
		const Engine::RenderFeatureExecutionPlan beforeLighting =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::BeforeLighting,
				Engine::RenderViewKind::Game);
		const Engine::RenderFeatureExecutionPlan afterLighting =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (!beforeLighting.IsValid() || beforeLighting.nodes.size() != 1 ||
			!afterLighting.IsValid() || afterLighting.nodes.size() != 2 ||
			afterLighting.nodes.front().selectionGroup ||
			afterLighting.nodes.back().selectionGroup ||
			afterLighting.nodes.front().selectionBegin ||
			afterLighting.nodes.back().selectionEnd ||
			afterLighting.nodes.back().source.pass ||
			afterLighting.sceneColorOutput.pass != composite.id) {

			return false;
		}
		restored.hierarchy[1].enabled = false;
		runtime.Rebuild(restored);
		const Engine::RenderFeatureExecutionPlan disabledHierarchyPlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (!disabledHierarchyPlan.nodes.empty() ||
			disabledHierarchyPlan.sceneColorOutput.pass) {

			return false;
		}

		profile.hierarchy[1].selection =
			Engine::RenderFeatureSelectionSettings{
				.mode = Engine::RenderFeatureSelectionMode::MaskedSceneColor,
				.anchor = Engine::RenderFeatureAnchor::AfterLighting,
				.renderingLayerMask = 1u << 3,
				.phaseMask = Engine::MakeRenderFeaturePhaseMask(
					Engine::RenderPhase::Opaque),
				.rendererMask = Engine::RenderFeatureRendererMask::Mesh,
			};
		const nlohmann::json selectiveData =
			Engine::RenderFeatureProfileSerializer::ToJson(profile);
		Engine::RenderFeatureProfileAsset selectiveProfile =
			Engine::RenderFeatureProfileSerializer::FromJson(selectiveData);
		Engine::RenderFeatureProfileAsset postProcessUISource = profile;
		postProcessUISource.passes[0].anchor =
			Engine::RenderFeatureAnchor::AfterPostProcessUI;
		const nlohmann::json postProcessUIData =
			Engine::RenderFeatureProfileSerializer::ToJson(postProcessUISource);
		const Engine::RenderFeatureProfileAsset postProcessUIProfile =
			Engine::RenderFeatureProfileSerializer::FromJson(postProcessUIData);
		if (postProcessUIProfile.passes.empty() ||
			postProcessUIProfile.passes[0].anchor !=
				Engine::RenderFeatureAnchor::AfterPostProcessUI ||
			postProcessUIData["passes"][0].value(
				"anchor", std::string{}) != "AfterPostProcessUI") {

			return false;
		}
		runtime.Rebuild(selectiveProfile);
		const Engine::RenderFeatureExecutionPlan selectivePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		Engine::RenderItem selectedItem{};
		selectedItem.backendID = Engine::RenderBackendID::Mesh;
		selectedItem.renderPhase = Engine::RenderPhase::Opaque;
		selectedItem.renderingLayerMask = 1u << 3;
		if (!selectivePlan.IsValid() || selectivePlan.nodes.size() != 2u ||
			!selectivePlan.nodes.front().selectionBegin ||
			!selectivePlan.nodes.back().selectionEnd ||
			!Engine::MatchesRenderFeatureSelection(selectedItem,
				selectiveProfile.hierarchy[1].selection)) {

			return false;
		}
		selectedItem.renderPhase = Engine::RenderPhase::ScreenUI;
		if (Engine::MatchesRenderFeatureSelection(selectedItem,
			selectiveProfile.hierarchy[1].selection)) {

			return false;
		}

		Engine::RenderFeatureProfileAsset standaloneProfile = profile;
		standaloneProfile.hierarchy[0].selection =
			Engine::RenderFeatureSelectionSettings{
				.mode = Engine::RenderFeatureSelectionMode::MaskedSceneColor,
				.anchor = Engine::RenderFeatureAnchor::BeforeLighting,
				.renderingLayerMask = 1u << 4,
				.phaseMask = Engine::MakeRenderFeaturePhaseMask(
					Engine::RenderPhase::Opaque),
				.rendererMask = Engine::RenderFeatureRendererMask::Mesh,
			};
		standaloneProfile = Engine::RenderFeatureProfileSerializer::FromJson(
			Engine::RenderFeatureProfileSerializer::ToJson(standaloneProfile));
		runtime.Rebuild(standaloneProfile);
		const Engine::RenderFeatureExecutionPlan standalonePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::BeforeLighting,
				Engine::RenderViewKind::Game);
		if (!standalonePlan.IsValid() || standalonePlan.nodes.size() != 1u ||
			!standalonePlan.nodes.front().selectionGroup ||
			standalonePlan.nodes.front().selectionGroup->type !=
				Engine::RenderFeatureHierarchyItemType::Pass ||
			!standalonePlan.nodes.front().selectionBegin ||
			!standalonePlan.nodes.front().selectionEnd ||
			standaloneProfile.hierarchy[0].selection.mode !=
				Engine::RenderFeatureSelectionMode::MaskedSceneColor) {

			return false;
		}

		selectiveProfile.hierarchy[1].selection.mode =
			Engine::RenderFeatureSelectionMode::IsolatedLayer;
		selectiveProfile.hierarchy[1].selection.phaseMask =
			Engine::MakeRenderFeaturePhaseMask(
				Engine::RenderPhase::Transparent);
		selectedItem.renderPhase = Engine::RenderPhase::Transparent;
		runtime.Rebuild(selectiveProfile);
		if (!runtime.IsItemIsolated(selectedItem)) {
			return false;
		}
		Engine::RenderFeatureRuntimeOverrides::GetInstance().SetGroupEnabled(
			"Reflection", false);
		const Engine::RenderFeatureExecutionPlan disabledRuntimePlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Game);
		if (runtime.IsItemIsolated(selectedItem) ||
			runtime.IsPassHierarchyEnabled(reflection.id) ||
			!disabledRuntimePlan.nodes.empty() ||
			disabledRuntimePlan.sceneColorOutput.pass) {

			return false;
		}
		Engine::RenderFeatureRuntimeOverrides::GetInstance().ResetAll();

		selectiveProfile.passes[1].sceneView = false;
		runtime.Rebuild(selectiveProfile);
		if (runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
			Engine::RenderViewKind::Scene).IsValid()) {

			return false;
		}
		selectiveProfile.passes[2].sceneView = false;
		runtime.Rebuild(selectiveProfile);
		const Engine::RenderFeatureExecutionPlan disabledSceneViewPlan =
			runtime.BuildPlan(Engine::RenderFeatureAnchor::AfterLighting,
				Engine::RenderViewKind::Scene);
		if (!disabledSceneViewPlan.nodes.empty() ||
			disabledSceneViewPlan.sceneColorOutput.pass) {

			return false;
		}

		profile.passes[0].anchor = Engine::RenderFeatureAnchor::AfterTransparent;
		runtime.Rebuild(profile);
		return !runtime.BuildPlan(
			Engine::RenderFeatureAnchor::AfterLighting,
			Engine::RenderViewKind::Game).IsValid();
	}
}

int main(int argc, char* argv[]) {

	if (1 < argc && std::string_view(argv[1]) == "--physics") {
		if (!TestRigidbody2DRestingContact() || !TestInactivePhysicsSystems() ||
			!TestEditCollisionState() ||
			!TestCapsuleCollisions()) {
			std::cerr << "Physics collision test failed\n";
			return 27;
		}
		std::cout << "Physics collision test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--paths") {
		if (!TestUTF8Path()) {
			std::cerr << "UTF-8 path failed\n";
			return 24;
		}
		std::cout << "UTF-8 path passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--texture-import") {
		if (!TestTextureImportSettings()) {
			std::cerr << "Texture import settings failed\n";
			return 25;
		}
		std::cout << "Texture import settings passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--ecs") {
		if (!TestECSChunkStorage() || !TestECSExternalStorage() ||
			!TestECSRuntimeData() || !TestNonTrivialDynamicBuffer() ||
			!TestTransformDirtyHierarchy() ||
			!TestTransformDimensionSerialization() ||
			!TestScreenSpaceOutlineSerialization()) {
			std::cerr << "ECS chunk storage failed\n";
			return 10;
		}
		std::cout << "ECS chunk storage passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--shader-graph") {

		if (!TestShaderGraphCompile() ||
			!TestRenderFeatureRuntimeOverrides() ||
			!TestRayTracingPipelineSerialization()) {
			std::cerr << "Shader Graph compilation failed\n";
			return 18;
		}
		std::cout << "Shader Graph compilation passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--materials") {

		if (!TestBlendStates() ||
			!TestMaterialParameters() ||
			!TestShaderReflectionMerge()) {
			std::cerr << "Material parameter storage failed\n";
			return 17;
		}
		std::cout << "Material parameter storage passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--render-features") {

		if (!TestRenderFeatureRuntimeOverrides() ||
			!TestShaderPathDependencies() ||
			!TestRenderFeatureProfile()) {
			std::cerr << "Render Feature test failed\n";
			return 22;
		}
		std::cout << "Render Feature test passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--render-feature-profile") {

		if (!TestRenderFeatureProfile()) {
			std::cerr << "Render Feature profile test failed\n";
			return 28;
		}
		std::cout << "Render Feature profile test passed\n";
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
	if (!TestUTF8Path()) {
		std::cerr << "UTF-8 path failed\n";
		return 24;
	}
	if (!TestTextureImportSettings()) {
		std::cerr << "Texture import settings failed\n";
		return 25;
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
	if (!TestTransformDirtyHierarchy()) {
		std::cerr << "Transform dirty hierarchy failed\n";
		return 14;
	}
	if (!TestTransformDimensionSerialization()) {
		std::cerr << "Transform dimension serialization failed\n";
		return 26;
	}
	if (!TestScreenSpaceOutlineSerialization()) {
		std::cerr << "Screen space outline serialization failed\n";
		return 29;
	}
	if (!TestRigidbody2DRestingContact()) {
		std::cerr << "Rigidbody2D resting contact failed\n";
		return 27;
	}
	if (!TestInactivePhysicsSystems()) {
		std::cerr << "Inactive physics systems failed\n";
		return 32;
	}
	if (!TestEditCollisionState()) {
		std::cerr << "Edit collision state failed\n";
		return 31;
	}
	if (!TestCapsuleCollisions()) {
		std::cerr << "Capsule collision failed\n";
		return 28;
	}
	if (!TestSerializationClone()) {
		std::cerr << "Serialization clone failed\n";
		return 15;
	}
	if (!TestMeshLODGeneration()) {
		std::cerr << "Mesh LOD generation failed\n";
		return 16;
	}
	if (!TestBlendStates()) {
		std::cerr << "Blend state failed\n";
		return 30;
	}
	if (!TestMaterialParameters()) {
		std::cerr << "Material parameter storage failed\n";
		return 17;
	}
	if (!TestShaderReflectionMerge()) {
		std::cerr << "Shader reflection merge failed\n";
		return 23;
	}
	if (!TestRenderFeatureRuntimeOverrides()) {
		std::cerr << "Render Feature runtime overrides failed\n";
		return 20;
	}
	if (!TestShaderPathDependencies()) {
		std::cerr << "Shader path dependencies failed\n";
		return 32;
	}
	if (!TestRayTracingPipelineSerialization()) {
		std::cerr << "Ray Tracing pipeline serialization failed\n";
		return 21;
	}
	if (!TestShaderGraphCompile()) {
		std::cerr << "Shader Graph compilation failed\n";
		return 18;
	}
	if (!TestRenderFeatureProfile()) {
		std::cerr << "RenderFeature profile failed\n";
		return 22;
	}
	std::cout << "NEMTests passed\n";
	return 0;
}
