#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace NEMTests {

	template <typename T>
	concept CanResizeBuffer = requires(T buffer) { buffer.Resize(1); };
	template <typename T>
	concept CanAddBufferElement = requires(T buffer) { buffer.Add(TestBufferElement{}); };
	template <typename T>
	concept CanEmplaceBufferElement = requires(T buffer) { buffer.EmplaceBack(); };
	template <typename T>
	concept CanClearBuffer = requires(T buffer) { buffer.Clear(); };
	template <typename T>
	concept CanReserveBuffer = requires(T buffer) { buffer.Reserve(1); };
	template <typename T>
	concept CanRemoveBufferElement = requires(T buffer) { buffer.RemoveAt(0); };
	template <typename T>
	concept CanSetBufferData = requires(T buffer) { buffer.SetData(nullptr, 0); };
	template <typename T>
	concept CanSetBufferElement = requires(T buffer) { buffer.SetElement(0, nullptr); };

	using ReadOnlyTestBuffer = Engine::DynamicBuffer<const TestBufferElement>;
	static_assert(!CanResizeBuffer<ReadOnlyTestBuffer> && !CanAddBufferElement<ReadOnlyTestBuffer> &&
		!CanEmplaceBufferElement<ReadOnlyTestBuffer> && !CanClearBuffer<ReadOnlyTestBuffer> &&
		!CanReserveBuffer<ReadOnlyTestBuffer> && !CanRemoveBufferElement<ReadOnlyTestBuffer>);
	static_assert(CanResizeBuffer<Engine::DynamicBuffer<TestBufferElement>>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>().GetData()), const TestBufferElement*>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>()[0]), const TestBufferElement&>);
	static_assert(std::is_same_v<decltype(std::declval<ReadOnlyTestBuffer>().GetSpan()), std::span<const TestBufferElement>>);
	static_assert(std::is_same_v<decltype(std::declval<Engine::ReadOnlyUntypedDynamicBuffer>().GetData()), const void*>);
	static_assert(!std::is_constructible_v<Engine::DynamicBuffer<TestBufferElement>, const Engine::DynamicBufferHeader*>);
	static_assert(!CanResizeBuffer<Engine::ReadOnlyUntypedDynamicBuffer> &&
		!CanRemoveBufferElement<Engine::ReadOnlyUntypedDynamicBuffer> && !CanSetBufferData<Engine::ReadOnlyUntypedDynamicBuffer> &&
		!CanSetBufferElement<Engine::ReadOnlyUntypedDynamicBuffer>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ECSWorld&>().TryGetUntypedBuffer({}, 0)),
		Engine::ReadOnlyUntypedDynamicBuffer>);
	static_assert(std::is_same_v<decltype(std::declval<const Engine::ECSWorld&>().GetBuffer<TestBufferElement>({})),
		ReadOnlyTestBuffer>);

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

		// const Worldの参照は型付きと型なしの両方で読取専用になる
		const Engine::ECSWorld& readWorld = world;
		auto readBuffer = readWorld.GetBuffer<TestBufferElement>(entity);
		auto readUntyped = readWorld.TryGetUntypedBuffer(entity, bufferTypeID);
		std::array<TestBufferElement, 2> readCopy{};
		if (!readBuffer.IsValid() || readBuffer.GetSize() != replacement.size() ||
			readBuffer[0].value != 13 || readBuffer.GetSpan()[2].value != 19 ||
			readWorld.GetBufferSpan<TestBufferElement>(entity).data() != readBuffer.GetData() ||
			readUntyped.CopyTo(readCopy.data(), 2, 1) != 2 || readCopy[0].value != 17 || readCopy[1].value != 19 ||
			readUntyped.CopyTo(nullptr, 2, 1) != 2 || readUntyped.CopyTo(readCopy.data(), 2, 3) != 0) {
			return false;
		}
		const Engine::Entity missing{};
		const Engine::Entity noBuffer = world.CreateEntity();
		const uint32_t nameTypeID = Engine::ComponentTypeRegistry::GetInstance().GetID<Engine::NameComponent>();
		if (readWorld.TryGetBuffer<TestBufferElement>(missing).IsValid() ||
			readWorld.TryGetBuffer<TestBufferElement>(noBuffer).IsValid() ||
			!readWorld.GetBufferSpan<TestBufferElement>(noBuffer).empty() ||
			readWorld.TryGetUntypedBuffer(noBuffer, bufferTypeID).IsValid() ||
			readWorld.TryGetUntypedBuffer(missing, bufferTypeID).IsValid() ||
			readWorld.TryGetUntypedBuffer(entity, nameTypeID).IsValid() ||
			readWorld.TryGetUntypedBuffer(entity, UINT32_MAX).IsValid() ||
			Engine::ReadOnlyUntypedDynamicBuffer{}.CopyTo(readCopy.data(), 2) != 0) {
			return false;
		}
		nlohmann::json savedBuffer;
		if (!readWorld.SerializeComponentToJson(entity, "TestBuffer", savedBuffer) ||
			savedBuffer != nlohmann::json::array({ 13, 17, 19 }) || readBuffer[0].value != 13) {
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

		const auto readUntyped = std::as_const(world).TryGetUntypedBuffer(source,
			Engine::ComponentTypeRegistry::GetInstance().GetID<Engine::ScriptEntry>());
		if (!readUntyped.IsValid() || readUntyped.IsTriviallyCopyable() || readUntyped.CopyTo(nullptr, 1) != 0) {
			return false;
		}

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
		Engine::ECSWorld world(Engine::ECSWorldKind::Authoring);
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
}
