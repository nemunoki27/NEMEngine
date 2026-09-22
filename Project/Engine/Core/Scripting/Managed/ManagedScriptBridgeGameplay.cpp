#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/Rendering/Meshes/Animation/SkinnedMeshAnimationManager.h>
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Engine {

	namespace {

		ECSWorld* ResolveTargetWorld(ManagedNativeEntity parent) {

			if (ECSWorld* fromParent = ResolveWorld(parent)) {
				return fromParent;
			}
			const SystemContext* context = ManagedScriptRuntime::GetCurrentContext();
			return context ? context->world : nullptr;
		}
	}


	ManagedNativeEntity ManagedScriptRuntime::ResolveEntityRefCallback(
		ManagedAssetGUID sourceAsset, uint64_t localFileID) {

		// localFileIDとsourceAssetから現在のworldのentityを引く
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : currentReferenceWorld_;
		if (!world || localFileID == 0) {
			return MakeNullNativeEntity();
		}

		const SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		const SceneInstance* activeScene = sceneInstances ? sceneInstances->GetActive() : nullptr;
		const UUID activeSceneInstanceID = activeScene ? activeScene->instanceID : UUID{};

		const AssetID sourceAssetID = ToAssetID(sourceAsset);
		Entity fallback = Entity::Null();
		Entity sourceMatch = Entity::Null();
		Entity activeMatch = Entity::Null();
		Entity activeSourceMatch = Entity::Null();
		world->ForEach<SceneObjectComponent>([&](Entity entity,
			SceneObjectComponent& sceneObject) {

			if (sceneObject.localFileID.value != localFileID) {
				return;
			}
			if (!world->IsAlive(fallback)) {
				fallback = entity;
			}
			const bool sourceMatched = !sourceAssetID ||
				sceneObject.sourceAsset == sourceAssetID;
			const bool activeMatched = activeSceneInstanceID &&
				sceneObject.sceneInstanceID == activeSceneInstanceID;
			if (sourceMatched && !world->IsAlive(sourceMatch)) {
				sourceMatch = entity;
			}
			if (activeMatched && !world->IsAlive(activeMatch)) {
				activeMatch = entity;
			}
			if (sourceMatched && activeMatched && !world->IsAlive(activeSourceMatch)) {
				activeSourceMatch = entity;
			}
			});

		const Entity entity = world->IsAlive(activeSourceMatch) ? activeSourceMatch :
			world->IsAlive(sourceMatch) ? sourceMatch :
			world->IsAlive(activeMatch) ? activeMatch : fallback;
		if (!world->IsAlive(entity)) {
			return MakeNullNativeEntity();
		}
		return MakeNativeEntity(*world, entity);
	}

	void ManagedScriptRuntime::GetEntityReferenceIdentityCallback(ManagedNativeEntity entity,
		ManagedAssetGUID* sourceAsset, uint64_t* localFileID, int32_t* kind) {

		if (sourceAsset) { *sourceAsset = {}; }
		if (localFileID) { *localFileID = 0; }
		if (kind) { *kind = 0; }

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		const SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved);
		if (!sceneObject || !sceneObject->localFileID) {
			return;
		}
		if (sourceAsset) { *sourceAsset = ToManagedAssetGUID(sceneObject->sourceAsset); }
		if (localFileID) { *localFileID = sceneObject->localFileID.value; }
		// runtime worldのentityはScene由来として扱う
		if (kind) { *kind = 1; }
	}

	ManagedNativeEntity ManagedScriptRuntime::CreateEntityCallback(const char* name, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world) {
			return MakeNullNativeEntity();
		}
		// 空Entityを即時予約する、emptyArchetypeへの行追加のみでコンポーネント追加つまりarchetype移行はflushへ
		const Entity reserved = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueCreateEntity(reserved, name ? name : "", parentEntity);
		return MakeNativeEntity(*world, reserved);
	}

	ManagedNativeEntity ManagedScriptRuntime::InstantiatePrefabCallback(ManagedAssetGUID prefabAssetID,
		ManagedVector3 position, ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveTargetWorld(parent);
		const AssetID prefabAsset = ToAssetID(prefabAssetID);
		if (!context || !world || !prefabAsset) {
			return MakeNullNativeEntity();
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.assetDatabase) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Prefab.Instantiate: AssetDatabaseが未設定のためPrefabを生成できません");
			return MakeNullNativeEntity();
		}

		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		PrefabInstantiateDesc desc{};
		desc.parent = parentEntity;
		if (world->IsAlive(parentEntity)) {
			if (const SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(parentEntity)) {
				desc.ownerSceneInstanceID = sceneObject->sceneInstanceID;
			}
		}
		if (!desc.ownerSceneInstanceID && services.sceneInstances) {
			if (const SceneInstance* activeScene = services.sceneInstances->GetActive()) {
				desc.ownerSceneInstanceID = activeScene->instanceID;
			}
		}

		HierarchySystem hierarchySystem{};
		PrefabSystem prefabSystem{};
		PrefabInstantiateResult result{};
		if (!prefabSystem.InstantiatePrefab(*services.assetDatabase, hierarchySystem, *world,
			prefabAsset, result, desc)) {

			for (auto it = result.createdEntities.rbegin(); it != result.createdEntities.rend(); ++it) {
				if (world->IsAlive(*it)) {
					world->DestroyEntity(*it);
				}
			}
			Logger::Output(LogType::Engine, spdlog::level::err,
				"Prefab.Instantiate: Prefabが存在しないかデータが不正です AssetID={}", ToString(prefabAsset));
			return MakeNullNativeEntity();
		}
		if (useTransform != 0) {
			if (TransformComponent* transform = world->TryGetComponent<TransformComponent>(result.root)) {
				transform->localPos = Vector3(position.x, position.y, position.z);
				transform->localRotation = Quaternion::Normalize(
					Quaternion(rotation.x, rotation.y, rotation.z, rotation.w));
			}
		}

		// 呼び出し元はSystemSchedulerが所有する可変ContextのためLifecycle同期へ戻す
		BehaviorSystem::SynchronizeInstantiatedEntities(
			*world, *const_cast<SystemContext*>(context), result.createdEntities);
		return MakeNativeEntity(*world, result.root);
	}

	int32_t ManagedScriptRuntime::DontDestroyOnLoadCallback(ManagedNativeEntity entity) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = ResolveWorld(entity);
		SceneInstanceManager* scenes = world ? world->GetCommandServices().sceneInstances : nullptr;
		if (!context || context->mode != WorldMode::Play || !scenes ||
			!scenes->DontDestroyOnLoad(*world, ResolveEntity(entity))) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"DontDestroyOnLoad: Play中の有効なルートEntityを指定してください");
			return 0;
		}
		return 1;
	}

	uint64_t ManagedScriptRuntime::LoadSceneAdditiveCallback(ManagedAssetGUID sceneAssetID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		const AssetID sceneAsset = ToAssetID(sceneAssetID);
		if (!world || !sceneAsset) {
			return 0;
		}
		// instance IDを先行採番してC#のSceneHandleと一致させ、load自体はflushへ回す
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneAdditive(instanceID, sceneAsset);
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::LoadSceneSingleCallback(ManagedAssetGUID sceneAssetID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		const AssetID sceneAsset = ToAssetID(sceneAssetID);
		if (!world) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: 実行中のWorldを取得できないため単一Sceneロードを拒否しました");
			return 0;
		}
		if (!sceneAsset) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: SceneAssetが無効なため単一Sceneロードを拒否しました");
			return 0;
		}
		SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		if (!sceneInstances) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: SceneInstanceManagerが未設定のため単一Sceneロードを拒否しました");
			return 0;
		}
		if (!sceneInstances->TryBeginSingleLoadRequest()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.LoadScene: 単一Sceneロード要求を処理中のため新しい要求を拒否しました");
			return 0;
		}
		// 単一ロード、新sceneをactiveにし旧sceneを全てアンロードする処理はflushで行う
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, sceneAsset);
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::ReloadActiveSceneCallback() {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: 実行中のWorldを取得できないため再読み込みを拒否しました");
			return 0;
		}
		SceneInstanceManager* sceneInstances =
			world->GetCommandServices().sceneInstances;
		if (!sceneInstances) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: SceneInstanceManagerが未設定のため再読み込みを拒否しました");
			return 0;
		}
		const SceneInstance* activeScene = sceneInstances->GetActive();
		const AssetID sceneAsset = activeScene ? activeScene->sceneAsset : AssetID{};
		if (!sceneAsset) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: アクティブSceneを取得できないため再読み込みを拒否しました");
			return 0;
		}
		if (!sceneInstances->TryBeginSingleLoadRequest()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SceneManager.ReloadActiveScene: 単一Sceneロード要求を処理中のため再読み込みを拒否しました");
			return 0;
		}

		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, sceneAsset);
		return instanceID.value;
	}

	void ManagedScriptRuntime::UnloadSceneCallback(uint64_t sceneInstanceID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceID == 0) {
			return;
		}
		world->GetCommandBuffer().EnqueueUnloadScene(UUID{ sceneInstanceID });
	}

	int32_t ManagedScriptRuntime::IsSceneInstanceAliveCallback(uint64_t sceneInstanceID) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceID == 0) {
			return 0;
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.sceneInstances) {
			return 0;
		}
		return services.sceneInstances->Find(UUID{ sceneInstanceID }) != nullptr ? 1 : 0;
	}

	void ManagedScriptRuntime::SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent, int32_t worldPositionStays) {

		EnqueueSetParentCommand(child, parent, worldPositionStays != 0);
	}
}
