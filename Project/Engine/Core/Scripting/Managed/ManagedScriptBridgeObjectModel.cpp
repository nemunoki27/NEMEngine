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
	//	オブジェクトモデルのコールバック
	//	C#側Entity/IComponentRef/ScriptBehaviour.Enabledから呼ばれる汎用操作
	//============================================================================

	int32_t ManagedScriptRuntime::GetComponentTypeIdCallback(const char* name) {

		// 安定なコンポーネント名からcompactなruntime type idを一度だけ解決しC#側でキャッシュする、未登録なら-1を返しC#側はHas=false扱いにする
		if (!name) {
			return -1;
		}
		const ComponentTypeInfo* info = ComponentTypeRegistry::GetInstance().FindByName(name);
		return info ? static_cast<int32_t>(info->id) : -1;
	}

	int32_t ManagedScriptRuntime::HasComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return 0;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeID) >= registry.GetComponentTypeCount()) {
			return 0;
		}
		// compact idから登録名を引いて存在判定する、O(1)のcomponentマスク照合
		return world->HasComponent(resolved, registry.GetInfo(static_cast<uint32_t>(typeID)).name) ? 1 : 0;
	}

	void ManagedScriptRuntime::AddComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeID) >= registry.GetComponentTypeCount()) {
			return;
		}
		// archetype移動を伴う構造変更はForEach走査を壊さないようWorldCommandBuffer経由で遅延適用する、重複追加や適用前のentity失効はApply側で再検証され安全に扱われる
		world->GetCommandBuffer().EnqueueAddComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeID)).name);
	}

	void ManagedScriptRuntime::RemoveComponentCallback(ManagedNativeEntity entity, int32_t typeID) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved) || typeID < 0) {
			return;
		}
		auto& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeID) >= registry.GetComponentTypeCount()) {
			return;
		}
		// 対象無しの削除や適用前のentity失効もApply側で安全に無処理になる
		world->GetCommandBuffer().EnqueueRemoveComponentByName(resolved, registry.GetInfo(static_cast<uint32_t>(typeID)).name);
	}

	void ManagedScriptRuntime::DestroyEntityCallback(ManagedNativeEntity entity) {

		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		// コールバック中の即時破棄は走査を壊すため安全地点まで遅延する、重複破棄はApply側で吸収する
		world->GetCommandBuffer().EnqueueDestroyEntity(resolved);
	}

	int32_t ManagedScriptRuntime::GetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID) {

		// 所有EntityとscriptSlotIDで実行中entryを特定し有効状態を返す、-1は未解決
		const Entity resolved = ResolveEntity(owner);
		return BehaviorSystem::GetScriptEnabled(resolved, UUID{ scriptSlotID });
	}

	void ManagedScriptRuntime::SetScriptEnabledCallback(ManagedNativeEntity owner, uint64_t scriptSlotID, int32_t enabled) {

		const Entity resolved = ResolveEntity(owner);
		BehaviorSystem::SetScriptEnabled(resolved, UUID{ scriptSlotID }, enabled != 0);
	}

	ManagedScriptInstanceHandle ManagedScriptRuntime::GetScriptInstanceCallback(ManagedNativeEntity owner, const char* scriptTypeID) {

		// owner Entity上でscriptTypeID一致のscript instanceを引き、ManagedBehaviorならC#側handleを返す
		if (!scriptTypeID) {
			return ManagedScriptInstanceHandle::Null();
		}
		const Entity resolved = ResolveEntity(owner);
		MonoBehavior* instance = BehaviorSystem::FindScriptInstance(resolved, scriptTypeID);
		// C#由来のscriptだけがhandleを持つ、C++ MonoBehaviorはNullになる
		ManagedBehavior* managed = dynamic_cast<ManagedBehavior*>(instance);
		return managed ? managed->GetManagedHandle() : ManagedScriptInstanceHandle::Null();
	}

} // Engine
