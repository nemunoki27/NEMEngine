#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	Object Model Callbacks
	//	C#側 Entity / IComponentRef / ScriptBehaviour.Enabled から呼ばれる generic な操作
	//============================================================================

	int32_t ManagedScriptRuntime::GetComponentTypeIdCallback(const char* name) {

		// 安定なコンポーネント名から compact な runtime type id を一度だけ解決する（C#側でキャッシュする）。
		// 未登録なら -1（C#側は Has=false 扱いにする）
		if (!name) {
			return -1;
		}
		const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(name);
		return info ? static_cast<int32_t>(info->id) : -1;
	}

	int32_t ManagedScriptRuntime::HasComponentCallback(ManagedNativeEntity entity, int32_t typeId) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeId < 0) {
			return 0;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeId) >= registry.GetComponentTypeCount()) {
			return 0;
		}
		// compact id から登録名を引いて存在判定する（O(1) の component mask 照合）
		return world->HasComponent(resolved, registry.GetInfo(static_cast<uint32_t>(typeId)).name) ? 1 : 0;
	}

	void ManagedScriptRuntime::AddComponentCallback(ManagedNativeEntity entity, int32_t typeId) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeId < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeId) >= registry.GetComponentTypeCount()) {
			return;
		}
		// archetype 移動を伴う構造変更は ForEach 走査を壊さないよう WorldCommandBuffer 経由で遅延適用する。
		// duplicate add / 適用前の entity 失効は Apply 側で再検証され安全に扱われる
		world->GetCommandBuffer().EnqueueAddComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeId)).name);
	}

	void ManagedScriptRuntime::RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeId) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeId < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeId) >= registry.GetComponentTypeCount()) {
			return;
		}
		// missing remove / 適用前の entity 失効も Apply 側で安全に no-op になる
		world->GetCommandBuffer().EnqueueRemoveComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeId)).name);
	}

	void ManagedScriptRuntime::DestroyEntityCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		// callback 中の即時破棄は走査を壊すため、安全地点まで遅延する。duplicate destroy は Apply 側で吸収する
		world->GetCommandBuffer().EnqueueDestroyEntity(resolved);
	}

	int32_t ManagedScriptRuntime::GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId) {

		// owner Entity + scriptSlotID で runtime entry を特定し、runtime enabled を返す（-1 は未解決）
		const Entity resolved = ResolveEntity(owner);
		return BehaviorSystem::GetScriptEnabled(resolved, UUID{ scriptSlotId });
	}

	void ManagedScriptRuntime::SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId, int32_t enabled) {

		const Entity resolved = ResolveEntity(owner);
		BehaviorSystem::SetScriptEnabled(resolved, UUID{ scriptSlotId }, enabled != 0);
	}

} // Engine
