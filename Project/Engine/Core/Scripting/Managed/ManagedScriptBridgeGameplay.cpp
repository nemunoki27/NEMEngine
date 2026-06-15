#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	Gameplay structural Callbacks
	//	Entity生成とPrefabとSceneとSetParent、構造変更はWorldCommandBuffer経由で遅延適用する
	//	生成系は空Entityを即時予約してhandleを返しcomponentとnameとparentはflushで適用する
	//============================================================================
	namespace {

		// callback実行中に対象とすべきworldを解決する、parentが有効ならそのworldを無効なら現在tickのactive worldつまりSystemContextを使う
		ECSWorld* ResolveTargetWorld(ManagedNativeEntity parent) {

			if (ECSWorld* fromParent = ResolveWorld(parent)) {
				return fromParent;
			}
			const SystemContext* context = ManagedScriptRuntime::GetCurrentContext();
			return context ? context->world : nullptr;
		}
	}

	ManagedNativeEntity ManagedScriptRuntime::CreateEntityCallback(const char* name, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world) {
			return MakeNullNativeEntity();
		}
		// 空Entityを即時予約する、emptyArchetypeへのrow追加のみでcomponent追加つまりarchetype migrationはflushへ
		const Entity reserved = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueCreateEntity(reserved, name ? name : "", parentEntity);
		return MakeNativeEntity(*world, reserved);
	}

	ManagedNativeEntity ManagedScriptRuntime::InstantiatePrefabCallback(uint64_t prefabAssetId,
		ManagedVector3 position, ManagedQuaternion rotation, int32_t useTransform, ManagedNativeEntity parent) {

		ECSWorld* world = ResolveTargetWorld(parent);
		if (!world || prefabAssetId == 0) {
			return MakeNullNativeEntity();
		}
		// ルートEntityを即時予約しPrefabSystemにはreservedRootを渡して実体化させる、deferredでも実rootを返す
		const Entity reservedRoot = world->CreateEntity();
		const Entity parentEntity = world->IsAlive(ResolveEntity(parent)) ? ResolveEntity(parent) : Entity::Null();
		world->GetCommandBuffer().EnqueueInstantiatePrefab(reservedRoot, UUID{ prefabAssetId },
			Vector3(position.x, position.y, position.z),
			Quaternion(rotation.x, rotation.y, rotation.z, rotation.w),
			useTransform != 0, parentEntity);
		return MakeNativeEntity(*world, reservedRoot);
	}

	uint64_t ManagedScriptRuntime::LoadSceneAdditiveCallback(uint64_t sceneAssetId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneAssetId == 0) {
			return 0;
		}
		// instance IDを先行採番してC#のSceneHandleと一致させ、load自体はflushへ回す
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneAdditive(instanceID, UUID{ sceneAssetId });
		return instanceID.value;
	}

	uint64_t ManagedScriptRuntime::LoadSceneSingleCallback(uint64_t sceneAssetId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneAssetId == 0) {
			return 0;
		}
		// 単一ロード、新sceneをactiveにし旧sceneを全てunloadする処理はflushで行う
		const UUID instanceID = UUID::New();
		world->GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, UUID{ sceneAssetId });
		return instanceID.value;
	}

	void ManagedScriptRuntime::UnloadSceneCallback(uint64_t sceneInstanceId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceId == 0) {
			return;
		}
		world->GetCommandBuffer().EnqueueUnloadScene(UUID{ sceneInstanceId });
	}

	int32_t ManagedScriptRuntime::IsSceneInstanceAliveCallback(uint64_t sceneInstanceId) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || sceneInstanceId == 0) {
			return 0;
		}
		const WorldCommandServices& services = world->GetCommandServices();
		if (!services.sceneInstances) {
			return 0;
		}
		return services.sceneInstances->Find(UUID{ sceneInstanceId }) != nullptr ? 1 : 0;
	}

	void ManagedScriptRuntime::SetParentKeepWorldCallback(ManagedNativeEntity child, ManagedNativeEntity parent, int32_t worldPositionStays) {

		ECSWorld* world = ResolveWorld(child);
		if (!world) {
			return;
		}
		const Entity childEntity = ResolveEntity(child);
		const Entity parentEntity = ResolveEntity(parent);
		world->GetCommandBuffer().EnqueueSetParent(childEntity, parentEntity, worldPositionStays != 0);
	}

	//============================================================================
	//	AudioSource gameplay method
	//	実際のvoice制御はAudioSourceSystemがruntimePlayRequestを消費して行い1フレーム遅延する
	//============================================================================
	namespace {

		// 対象entityのAudioSourceComponentを取得する、stale entityやmissing componentはnullptr
		AudioSourceComponent* ResolveAudioSource(ManagedNativeEntity entity) {
			ECSWorld* world = ResolveWorld(entity);
			if (!world) {
				return nullptr;
			}
			const Entity resolved = ResolveEntity(entity);
			return world->IsAlive(resolved) ? world->TryGetComponent<AudioSourceComponent>(resolved) : nullptr;
		}
	}

	void ManagedScriptRuntime::AudioPlayCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 1;
		}
	}

	void ManagedScriptRuntime::AudioPauseCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 2;
		}
	}

	void ManagedScriptRuntime::AudioStopCallback(ManagedNativeEntity entity) {
		if (AudioSourceComponent* audio = ResolveAudioSource(entity)) {
			audio->runtimePlayRequest = 3;
		}
	}

	int32_t ManagedScriptRuntime::AudioIsPlayingCallback(ManagedNativeEntity entity) {
		const AudioSourceComponent* audio = ResolveAudioSource(entity);
		// pause中は再生中扱いにしない
		return (audio && audio->runtimePlaying && !audio->runtimePaused) ? 1 : 0;
	}

} // Engine
