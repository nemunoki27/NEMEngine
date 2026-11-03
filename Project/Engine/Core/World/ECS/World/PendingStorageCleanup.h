#pragma once

//============================================================================
//	include
//============================================================================
#include "ECSWorld.h"
#include "PendingComponent.h"

namespace Engine {

	// 追加取消では外部領域を解放し、公開成功後はComponentへ所有を移す
	template <typename Component, typename Storage, typename Handle>
	void TrackPendingStorage(ECSWorld& world, Entity entity, Handle Component::* member) {

		if (world.HasComponent<Component>(entity)) return;
		const auto typeID = ComponentTypeRegistry::GetInstance().GetID<Component>();
		auto* pending = world.GetCommandBuffer().FindPendingComponent(entity, typeID);
		if (!pending) return;
		const auto lifetime = world.GetLifetime();
		pending->SetExternalCleanup([&world, entity, lifetime, member](void* data) {
			if (!lifetime->IsAlive()) return;
			const auto& value = *static_cast<Component*>(data);
			const auto* published = world.TryGetComponent<Component>(entity);
			if (!published || published->*member != value.*member) {
				world.GetStorage().Get<Storage>().Release(value.*member);
			}
		});
	}
}
