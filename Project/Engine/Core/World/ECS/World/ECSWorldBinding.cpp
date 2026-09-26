#include "ECSWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/PendingComponent.h>

//============================================================================
//	ECSWorld classMethods
//============================================================================
uint64_t Engine::ECSWorld::GetBindingComponentInstanceID(const Entity& entity, uint32_t typeID) const {

	if (!IsAlive(entity) || IsPendingDestroy(entity)) {
		return 0;
	}
	if (const uint64_t current = GetComponentInstanceID(entity, typeID)) {
		return current;
	}
	const PendingComponent* pending = commandBuffer_.FindPendingComponent(entity, typeID);
	return pending ? pending->GetInstanceID() : 0;
}

void* Engine::ECSWorld::TryGetPendingComponentData(const Entity& entity, uint32_t typeID) {

	if (!IsAlive(entity) || IsPendingDestroy(entity)) {
		return nullptr;
	}
	PendingComponent* pending = commandBuffer_.FindPendingComponent(entity, typeID);
	return pending ? pending->GetData() : nullptr;
}

Engine::UntypedDynamicBuffer Engine::ECSWorld::TryGetBufferForBinding(const Entity& entity, uint32_t typeID) {

	if (!IsAlive(entity) || IsPendingDestroy(entity)) {
		return {};
	}
	if (UntypedDynamicBuffer buffer = TryGetUntypedBuffer(entity, typeID); buffer.IsValid()) {
		return buffer;
	}
	const auto& info = ComponentTypeRegistry::GetInstance().GetInfo(typeID);
	if (info.storageKind != ComponentStorageKind::Buffer) {
		return {};
	}
	// 追加前のBufferも通常と同じ操作で編集する
	return UntypedDynamicBuffer(static_cast<DynamicBufferHeader*>(TryGetPendingComponentData(entity, typeID)),
		info.elementSize, info.elementAlign, info.bufferElementTriviallyCopyable);
}

void Engine::ECSWorld::ApplyPendingComponent(const Entity& entity, const PendingComponent& component) {

	if (component.GetInstanceID() == 0 || !IsAlive(entity) || IsPendingDestroy(entity) ||
		HasComponent(entity, component.GetInfo().id)) {
		return;
	}
	const EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
	EntitySignature newSignature = oldSignature;
	newSignature.Set(component.GetInfo().id);
	// 予約時の値と個体番号を追加後も引き継ぐ
	MigrateEntity(entity, oldSignature, newSignature, &component);
	CompleteComponentChange(entity, component.GetInfo().id, ComponentMutationKind::Added);
}
