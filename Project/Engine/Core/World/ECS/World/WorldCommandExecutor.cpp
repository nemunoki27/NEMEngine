#include "WorldCommandExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
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

	// entityとその子孫をまとめて破棄予約する
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

	// childをnewParentの子にすると循環するか、newParentの祖先にchildが居るか
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

	// worldPositionStays:親変更前のchild world姿勢を、変更後の親追従Transform基準のlocal値へ落として維持する
	void PreserveWorldTransform(ECSWorld& world, const Entity& child,
		const ResolvedWorldTransform& childWorldBefore) {

		TransformComponent* childTransform = world.TryGetComponent<TransformComponent>(child);
		if (!childTransform) {
			return;
		}
		ResolvedWorldTransform parentFollow{};
		if (!TransformWorldUtility::ResolveParentFollowTransform(world, child, parentFollow)) {
			return;
		}

		const Vector3 worldPos = childWorldBefore.matrix.GetTranslationValue();
		childTransform->localPos = Vector3::Transform(
			worldPos, Matrix4x4::Inverse(parentFollow.matrix));
		childTransform->localRotation = Quaternion::Normalize(
			Quaternion::Inverse(parentFollow.rotation) * childWorldBefore.rotation);
		childTransform->localScale = Vector3(
			parentFollow.scale.x != 0.0f ? childWorldBefore.scale.x / parentFollow.scale.x : childWorldBefore.scale.x,
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
	case WorldCommandKind::AddComponentByName:

		// 同一componentのAdd/Removeが混在しても、enqueue順(=呼び出し順)で決定的に適用する
		world.AddComponentByName(command.target, command.text);
		break;
	case WorldCommandKind::RemoveComponentByName:

		world.RemoveComponentByName(command.target, command.text);
		break;
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

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: 破棄予約済みEntityは親に設定できません");
			break;
		}
		// 親が破棄済みならルート化する
		Entity parent = world.IsAlive(command.parent) ? command.parent : Entity::Null();
		// 循環を作る付け替えは拒否する、childがparentの祖先になるケース
		if (world.IsAlive(parent) && WouldCreateCycle(world, command.target, parent)) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: 親子階層が循環するためSetParentを拒否しました");
			break;
		}

		// worldPositionStays:付け替え前のworld姿勢を控えておき、付け替え後にlocalへ落として復元する
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

		// 予約済みの空EntityをGameObjectとしてmaterializeしTransform/SceneObject/Nameを付与する
		SceneAuthoring::EnsureGameObjectDefaults(world, command.target);
		// 生成EntityをsceneInstanceへ所属させる、InstantiatePrefabと同様に未設定だと描画フィルタで除外される
		const WorldCommandServices& services = world.GetCommandServices();
		if (SceneObjectComponent* sceneObject = world.TryGetComponent<SceneObjectComponent>(command.target)) {
			if (world.IsAlive(command.parent)) {
				if (const SceneObjectComponent* parentSceneObject = world.TryGetComponent<SceneObjectComponent>(command.parent)) {
					sceneObject->sceneInstanceID = parentSceneObject->sceneInstanceID;
				}
			}
			if (!sceneObject->sceneInstanceID && services.sceneInstances) {
				if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
					sceneObject->sceneInstanceID = activeScene->instanceID;
				}
			}
		}
		if (!command.text.empty()) {
			NameComponent* nameComponent = world.TryGetComponent<NameComponent>(command.target);
			if (!nameComponent) {
				nameComponent = &world.AddComponent<NameComponent>(command.target);
			}
			nameComponent->name = command.text;
		}
		// callback中にstagingされた初期SRTを適用する、pending中のTransform書き込み
		if (TransformComponent* transform = world.TryGetComponent<TransformComponent>(command.target)) {
			if (command.flags & FlagHasPosition) {
				transform->localPos = command.position;
			}
			if (command.flags & FlagHasRotation) {
				transform->localRotation = Quaternion::Normalize(command.rotation);
			}
			if (command.flags & FlagHasScale) {
				transform->localScale = command.scale;
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

	const WorldCommandServices& services = world.GetCommandServices();
	if (!services.sceneInstances || !services.assetDatabase || !services.sceneSystem) {
		if (command.kind == WorldCommandKind::LoadSceneSingle && services.sceneInstances) {
			services.sceneInstances->ClearSingleLoadRequest();
		}
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"WorldCommandBuffer: WorldにCommandServiceが未設定のためScene Commandを処理できません");
		return;
	}
	if (command.kind == WorldCommandKind::LoadSceneAdditive) {
		services.sceneInstances->LoadAdditive(*services.assetDatabase, *services.sceneSystem, world,
			command.assetID, command.sceneInstanceID);
	} else if (command.kind == WorldCommandKind::LoadSceneSingle) {

		// 単一ロード、UnityのadditiveでないLoadScene相当
		// 新sceneをloadする前に現在ロード中のsceneIDを退避し、新sceneをactiveにしてから旧sceneを全てunloadする
		std::vector<UUID> previousScenes;
		previousScenes.reserve(services.sceneInstances->GetAll().size());
		for (const SceneInstance& scene : services.sceneInstances->GetAll()) {
			if (!scene.persistent) {
				previousScenes.emplace_back(scene.instanceID);
			}
		}
		const bool loaded = services.sceneInstances->LoadAdditive(*services.assetDatabase,
			*services.sceneSystem, world, command.assetID,
			command.sceneInstanceID);
		services.sceneInstances->ClearSingleLoadRequest();
		if (!loaded) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"WorldCommandBuffer: SceneAssetを読み込めないため単一Sceneロードを拒否しました AssetID={}",
				ToString(command.assetID));
			return;
		}
		services.sceneInstances->SetActive(command.sceneInstanceID);
		// 新scene以外の旧sceneを全てunloadする
		for (const UUID& previous : previousScenes) {
			if (previous != command.sceneInstanceID) {
				services.sceneInstances->Unload(world, previous);
			}
		}
	} else {
		services.sceneInstances->Unload(world, command.sceneInstanceID);
	}
}
