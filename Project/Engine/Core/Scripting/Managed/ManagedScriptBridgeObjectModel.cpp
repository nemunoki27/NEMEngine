#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Scripting/Managed/ManagedBehavior.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	Object Model Callbacks
	//	C#側Entity / IComponentRef / ScriptBehaviour.Enabledから呼ばれるgenericな操作
	//============================================================================

	int32_t ManagedScriptRuntime::GetComponentTypeIdCallback(const char* name) {

		// 安定なコンポーネント名からcompactなruntime type idを一度だけ解決しC#側でキャッシュする、未登録なら-1を返しC#側はHas=false扱いにする
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
		// compact idから登録名を引いて存在判定する、O(1)のcomponent mask照合
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
		// archetype移動を伴う構造変更はForEach走査を壊さないようWorldCommandBuffer経由で遅延適用する、duplicate addや適用前のentity失効はApply側で再検証され安全に扱われる
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
		// missing remove /適用前のentity失効もApply側で安全にno-opになる
		world->GetCommandBuffer().EnqueueRemoveComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeId)).name);
	}

	void ManagedScriptRuntime::DestroyEntityCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		// callback中の即時破棄は走査を壊すため安全地点まで遅延する、duplicate destroyはApply側で吸収する
		world->GetCommandBuffer().EnqueueDestroyEntity(resolved);
	}

	int32_t ManagedScriptRuntime::GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId) {

		// owner EntityとscriptSlotIDでruntime entryを特定しruntime enabledを返す、-1は未解決
		const Entity resolved = ResolveEntity(owner);
		return BehaviorSystem::GetScriptEnabled(resolved, UUID{ scriptSlotId });
	}

	void ManagedScriptRuntime::SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotId, int32_t enabled) {

		const Entity resolved = ResolveEntity(owner);
		BehaviorSystem::SetScriptEnabled(resolved, UUID{ scriptSlotId }, enabled != 0);
	}

	ManagedScriptInstanceHandle ManagedScriptRuntime::GetScriptInstanceCallback(ManagedNativeEntity owner, const char* scriptTypeId) {

		// owner Entity上でscriptTypeId一致のscript instanceを引き、ManagedBehaviorならC#側handleを返す
		if (!scriptTypeId) {
			return ManagedScriptInstanceHandle::Null();
		}
		const Entity resolved = ResolveEntity(owner);
		MonoBehavior* instance = BehaviorSystem::FindScriptInstance(resolved, scriptTypeId);
		// C#由来のscriptだけがhandleを持つ、C++ MonoBehaviorはNullになる
		ManagedBehavior* managed = dynamic_cast<ManagedBehavior*>(instance);
		return managed ? managed->GetManagedHandle() : ManagedScriptInstanceHandle::Null();
	}

} // Engine
