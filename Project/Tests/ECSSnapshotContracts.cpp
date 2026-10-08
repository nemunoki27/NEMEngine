#include "TestContracts.h"
#include "ECSBufferContractChecks.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
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

	bool TestSerializationClone() {

		RegisterTestComponents();
		// 追加待ちの値と従属BufferをFlushせず保存する
		Engine::ECSWorld pendingWorld;
		const auto pendingEntity = pendingWorld.CreateEntity();
		auto& commands = pendingWorld.GetCommandBuffer();
		auto& registry = Engine::ComponentTypeRegistry::GetInstance();
		commands.EnqueueCreateEntity(pendingWorld, pendingEntity, "PendingSnapshot", Engine::Entity::Null());
		commands.StageAddComponent(pendingWorld, pendingEntity, registry.GetID<Engine::ScriptComponent>());
		const uint32_t scriptBufferID = registry.GetID<Engine::ScriptEntry>();
		commands.StageAddComponent(pendingWorld, pendingEntity, scriptBufferID);
		auto script = Engine::MakeScriptEntry("pending-script", "Game.Pending");
		script.serializedFields["value"] = 73;
		pendingWorld.TryGetBufferForBinding<Engine::ScriptEntry>(pendingEntity).Add(script);
		const Engine::ECSWorld& readPending = pendingWorld;
		static_assert(!CanResizeBuffer<decltype(readPending.TryGetBufferForBinding<Engine::ScriptEntry>(pendingEntity))>);
		static_assert(std::is_same_v<decltype(readPending.TryGetComponentForBinding<Engine::NameComponent>(pendingEntity)),
			const Engine::NameComponent*>);
		if (readPending.TryGetComponentForBinding<Engine::NameComponent>(pendingEntity)->name != "PendingSnapshot" ||
			readPending.TryGetBufferForBinding(pendingEntity, scriptBufferID).GetSize() != 1) return false;
		nlohmann::json pendingJSON;
		pendingWorld.SerializeEntityComponents(pendingEntity, pendingJSON);
		nlohmann::json pendingScript;
		if (!pendingWorld.SerializeComponentToJson(pendingEntity, "Script", pendingScript) ||
			pendingScript != pendingJSON["Script"] || !pendingJSON.contains("Name") ||
			!pendingScript.is_array() || pendingScript.size() != 1 || pendingScript[0]["serializedFields"]["value"] != 73 ||
			pendingWorld.HasComponent<Engine::ScriptComponent>(pendingEntity) || commands.IsEmpty()) return false;
		auto pendingSnapshot = pendingWorld.CloneForSerialization();
		if (!pendingSnapshot->HasComponent<Engine::NameComponent>(pendingEntity) ||
			!pendingSnapshot->HasBuffer<Engine::ScriptEntry>(pendingEntity) || !pendingSnapshot->GetCommandBuffer().IsEmpty() ||
			pendingWorld.HasComponent<Engine::NameComponent>(pendingEntity)) return false;
		pendingWorld.TryGetBufferForBinding<Engine::ScriptEntry>(pendingEntity)[0].serializedFields["value"] = 89;
		nlohmann::json snapshotJSON;
		pendingSnapshot->SerializeEntityComponents(pendingEntity, snapshotJSON);
		if (snapshotJSON != pendingJSON) return false;
		pendingSnapshot.reset();
		pendingWorld.TryGetBufferForBinding<Engine::ScriptEntry>(pendingEntity)[0].serializedFields["value"] = 73;
		commands.StageAddComponent(pendingWorld, pendingEntity, registry.GetID<TestEnableableComponent>());
		commands.EnqueueRemoveComponentByName(pendingEntity, registry.GetInfo(registry.GetID<TestEnableableComponent>()).name);
		nlohmann::json cancelledJSON;
		pendingWorld.SerializeEntityComponents(pendingEntity, cancelledJSON);
		if (cancelledJSON != pendingJSON) return false;
		// 保存用複製へ保留中の名前・有効状態・親変更を反映する
		const auto parentEntity = pendingWorld.CreateEntity();
		commands.EnqueueCreateEntity(pendingWorld, parentEntity, "PendingParent", Engine::Entity::Null());
		commands.EnqueueSetParent(pendingEntity, parentEntity);
		commands.EnqueueSetNameEnsuringComponent(pendingEntity, "RenamedBeforeSave");
		commands.EnqueueSetActiveSelfEnsuringComponent(pendingEntity, false);
		auto commandSnapshot = pendingWorld.CloneForSerialization();
		if (pendingWorld.TryGetComponentForBinding<Engine::NameComponent>(pendingEntity)->name != "PendingSnapshot" ||
			!commandSnapshot ||
			commandSnapshot->GetComponent<Engine::NameComponent>(pendingEntity).name != "RenamedBeforeSave" ||
			commandSnapshot->GetComponent<Engine::SceneObjectComponent>(pendingEntity).activeSelf ||
			commandSnapshot->GetComponent<Engine::HierarchyComponent>(pendingEntity).parent != parentEntity) {
			return false;
		}
		pendingWorld.FlushWorldCommands();
		nlohmann::json appliedScript;
		if (!pendingWorld.SerializeComponentToJson(pendingEntity, "Script", appliedScript) ||
			appliedScript != pendingScript) return false;
		const auto appliedEntries = Engine::GetScriptEntries(readPending, pendingEntity);
		if (appliedEntries.size() != 1 || appliedEntries.front().serializedFields["value"] != 73) return false;
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
		// 元Worldの変更が保存用コピーへ漏れていないことを確認する
		if (snapshot->GetComponent<Engine::NameComponent>(entity).name != "SnapshotSource" ||
			snapshotEntries.front().serializedFields.value("value", 0) != 24) {
			return false;
		}
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
