#include "WorldCommandExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace {

	using namespace Engine;

	// 論理的な子孫から順番に破棄を予約する
	void DestroyEntitySubtree(ECSWorld& world, const Entity& entity) {

		const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, entity);
		HierarchySystem hierarchySystem{};
		for (auto it = entities.rbegin(); it != entities.rend(); ++it) {

			if (world.IsAlive(*it)) {
				hierarchySystem.SetParent(world, *it, Entity::Null());
				world.DestroyEntity(*it);
			}
		}
	}

	// 親の祖先を走査して循環を検出する
	bool WouldCreateCycle(ECSWorld& world, const Entity& child, const Entity& newParent) {

		Entity current = newParent;
		for (int32_t guard = 0; guard < 4096; ++guard) {
			if (!world.IsAlive(current)) {
				return false;
			}
			if (current == child) {
				return true;
			}
			HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
			if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
				return false;
			}
			current = hierarchy->parent;
		}
		return true;
	}

	// 変更前のWorld姿勢を新しい親のLocal姿勢へ変換する
	void PreserveWorldTransform(ECSWorld& world, const Entity& child, const ResolvedWorldTransform& childWorldBefore) {

		TransformComponent* childTransform = world.TryGetComponent<TransformComponent>(child);
		if (!childTransform) {
			return;
		}
		ResolvedWorldTransform parentFollow{};
		if (!TransformWorldUtility::ResolveParentFollowTransform(world, child, parentFollow)) {
			return;
		}

		const Vector3 worldPos = childWorldBefore.matrix.GetTranslationValue();
		childTransform->localPos = Vector3::Transform(worldPos, Matrix4x4::Inverse(parentFollow.matrix));
		childTransform->localRotation =
			Quaternion::Normalize(Quaternion::Inverse(parentFollow.rotation) * childWorldBefore.rotation);
		childTransform->localScale =
			Vector3(parentFollow.scale.x != 0.0f ? childWorldBefore.scale.x / parentFollow.scale.x : childWorldBefore.scale.x,
				parentFollow.scale.y != 0.0f ? childWorldBefore.scale.y / parentFollow.scale.y : childWorldBefore.scale.y,
				parentFollow.scale.z != 0.0f ? childWorldBefore.scale.z / parentFollow.scale.z : childWorldBefore.scale.z);
	}
}

//============================================================================
//	WorldCommandExecutor classMethods
//============================================================================
void Engine::WorldCommandExecutor::Apply(ECSWorld& world, const WorldCommand& command) {

	// Sceneコマンドはtarget Entityを持たないため、IsAlive検証より前に処理する
	if (command.kind == WorldCommandKind::LoadSceneAdditive || command.kind == WorldCommandKind::LoadSceneSingle ||
		command.kind == WorldCommandKind::UnloadScene) {

		ApplyScene(world, command);
		return;
	}

	// 積まれてから破棄された可能性があるため、適用直前に必ず再検証する
	if (!world.IsAlive(command.target)) {
		return;
	}
	if (world.IsPendingDestroy(command.target)) {
		return;
	}

	switch (command.kind) {
	case WorldCommandKind::DestroyEntity:

		// 同一エンティティへの重複Destroyはpendingで安全に無視される
		DestroyEntitySubtree(world, command.target);
		break;
	case WorldCommandKind::AddComponentValue:

		world.ApplyPendingComponent(command.target, *command.component);
		break;
	case WorldCommandKind::AddComponentByName:

		// 追加と削除は予約した順番で適用する
		world.AddComponentByName(command.target, command.text);
		break;
	case WorldCommandKind::RemoveComponentByName:

		world.RemoveComponentByName(command.target, command.text);
		break;
	case WorldCommandKind::RemoveScript: {

		// 保存slotで照合し、同型の別Scriptは残す
		if (!world.HasBuffer<ScriptEntry>(command.target)) {
			break;
		}
		auto entries = world.GetBuffer<ScriptEntry>(command.target);
		const auto& registry = ComponentTypeRegistry::GetInstance();
		const uint32_t bufferTypeID = registry.GetID<ScriptEntry>();
		const uint32_t scriptTypeID = registry.GetID<ScriptComponent>();
		const uint64_t bufferInstanceID = world.GetComponentInstanceID(command.target, bufferTypeID);
		const uint64_t scriptInstanceID = world.GetComponentInstanceID(command.target, scriptTypeID);
		for (uint32_t index = 0; index < entries.GetSize(); ++index) {
			if (entries[index].scriptSlotID != command.scriptSlotID) {
				continue;
			}
			entries.RemoveAt(index);
			world.MarkComponentModified<ScriptEntry>(command.target);
			// 通知中に置き換わったScriptやBufferへ戻らない
			if (world.GetComponentInstanceID(command.target, bufferTypeID) != bufferInstanceID ||
				world.GetComponentInstanceID(command.target, scriptTypeID) != scriptInstanceID) {
				break;
			}
			world.MarkComponentModified<ScriptComponent>(command.target);
			if (world.GetComponentInstanceID(command.target, bufferTypeID) != bufferInstanceID ||
				world.GetComponentInstanceID(command.target, scriptTypeID) != scriptInstanceID) {
				break;
			}
			// 構造移動後のBufferから残りのslotを確認する
			if (world.GetBuffer<ScriptEntry>(command.target).GetSize() == 0) {
				world.RemoveComponent<ScriptComponent>(command.target);
			}
			break;
		}
		break;
	}
	case WorldCommandKind::SetNameEnsuringComponent: {

		NameComponent* nameComponent = world.TryGetComponent<NameComponent>(command.target);
		if (!nameComponent) {
			nameComponent = &world.AddComponent<NameComponent>(command.target);
		}
		nameComponent->name = command.text;
		break;
	}
	case WorldCommandKind::SetActiveSelfEnsuringComponent: {

		SceneObjectUtility::SetActiveSelf(world, command.target, command.boolValue);
		break;
	}
	case WorldCommandKind::SetParent: {

		if (world.IsAlive(command.parent) && world.IsPendingDestroy(command.parent)) {

			Logger::Output(LogType::Engine, spdlog::level::warn, "WorldCommandBuffer: 破棄予約済みEntityは親に設定できません");
			break;
		}
		// 親が破棄済みならルート化する
		Entity parent = world.IsAlive(command.parent) ? command.parent : Entity::Null();
		// 親子階層が循環する変更を拒否する
		if (world.IsAlive(parent) && WouldCreateCycle(world, command.target, parent)) {
			Logger::Output(
				LogType::Engine, spdlog::level::warn, "WorldCommandBuffer: 親子階層が循環するためSetParentを拒否しました");
			break;
		}

		// 親変更前のWorld姿勢を保持する
		ResolvedWorldTransform worldBefore{};
		bool keepWorld = command.boolValue;
		if (keepWorld) {
			keepWorld = TransformWorldUtility::ResolveWorldTransform(world, command.target, worldBefore);
		}

		HierarchySystem hierarchySystem{};
		hierarchySystem.SetParent(world, command.target, parent);

		if (keepWorld) {
			PreserveWorldTransform(world, command.target, worldBefore);
		}
		break;
	}
	case WorldCommandKind::CreateEntity: {

		// 予約したEntityの初期Componentを確定する
		SceneAuthoring::EnsureGameObjectDefaults(world, command.target);
		// 親かActive Sceneの所属を引き継ぐ
		const WorldCommandServices& services = world.GetCommandServices();
		if (SceneObjectComponent* sceneObject = world.TryGetComponent<SceneObjectComponent>(command.target)) {
			if (world.IsAlive(command.parent)) {
				if (const SceneObjectComponent* parentSceneObject =
						world.TryGetComponent<SceneObjectComponent>(command.parent)) {
					sceneObject->sceneInstanceID = parentSceneObject->sceneInstanceID;
				}
			}
			if (!sceneObject->sceneInstanceID && services.sceneInstances) {
				if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
					sceneObject->sceneInstanceID = activeScene->instanceID;
				}
			}
		}
		// 親付けはTransform確定後に行う
		if (world.IsAlive(command.parent) && !WouldCreateCycle(world, command.target, command.parent)) {
			HierarchySystem hierarchySystem{};
			hierarchySystem.SetParent(world, command.target, command.parent);
		}
		break;
	}
	}
}

void Engine::WorldCommandExecutor::ApplyScene(ECSWorld& world, const WorldCommand& command) {

	const auto lifetime = world.GetLifetime();
	const uint64_t serviceRevision = world.GetCommandServiceRevision();
	const WorldCommandServices services = world.GetCommandServices();
	const auto connected = [&]() {
		lifetime->ThrowIfEnded();
		if (world.GetCommandServiceRevision() != serviceRevision) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: Scene操作中に接続が変更されたため後続処理を中止しました");
			return false;
		}
		return true;
	};
	if (!services.sceneInstances || !services.assetDatabase || !services.sceneSystem) {
		if (command.kind == WorldCommandKind::LoadSceneSingle && services.sceneInstances) {
			services.sceneInstances->ClearSingleLoadRequest();
		}
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"WorldCommandBuffer: WorldにCommandServiceが未設定のためScene Commandを処理できません");
		return;
	}
	if (command.kind == WorldCommandKind::LoadSceneAdditive) {
		services.sceneInstances->LoadAdditive(
			*services.assetDatabase, *services.sceneSystem, world, command.assetID, command.sceneInstanceID);
	} else if (command.kind == WorldCommandKind::LoadSceneSingle) {

		// 新Sceneの読み込み成功後に旧Sceneを解放する
		std::vector<UUID> previousScenes;
		previousScenes.reserve(services.sceneInstances->GetAll().size());
		for (const SceneInstance& scene : services.sceneInstances->GetAll()) {
			if (!scene.persistent) {
				previousScenes.emplace_back(scene.instanceID);
			}
		}
		const bool loaded = services.sceneInstances->LoadAdditive(
			*services.assetDatabase, *services.sceneSystem, world, command.assetID, command.sceneInstanceID);
		// 読込通知後に別の接続へ旧Sceneの破棄を渡さない
		if (!connected()) {
			return;
		}
		services.sceneInstances->ClearSingleLoadRequest();
		if (!loaded) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: SceneAssetを読み込めないため単一Sceneロードを拒否しました AssetID={}",
				ToString(command.assetID));
			return;
		}
		services.sceneInstances->SetActive(command.sceneInstanceID);
		// 常駐Sceneと新Sceneを残して旧Sceneを解放する
		for (const UUID& previous : previousScenes) {
			if (previous != command.sceneInstanceID) {
				services.sceneInstances->Unload(world, previous);
				if (!connected()) {
					return;
				}
			}
		}
	} else {
		services.sceneInstances->Unload(world, command.sceneInstanceID);
	}
}
