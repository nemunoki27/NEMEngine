#include "ManagedWorldRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace {

	// nullハンドルかどうか
	constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;
}

//============================================================================
//	ManagedWorldRegistry classMethods
//============================================================================
Engine::ManagedWorldHandle Engine::ManagedWorldRegistry::Register(ECSWorld& world) {

	// 既に登録済みなら同じハンドルを返し暗黙の二重登録を作らない
	const ManagedWorldHandle existing = TryGetHandle(world);
	if (existing.index != kInvalidIndex) {
		return existing;
	}

	// 同じアドレスに残った終了済みWorldの登録を外す
	const auto stale = handles_.find(&world);
	if (stale != handles_.end()) {
		Unregister(stale->second);
	}

	// 検索表が完成してからハンドルを公開する
	const auto slot = worlds_.Emplace(WorldSlot{ &world, world.GetLifetime() });
	const ManagedWorldHandle handle{ slot.index, slot.generation };
	try {
		handles_.emplace(&world, handle);
	} catch (...) {
		worlds_.Release(slot);
		throw;
	}
	return handle;
}

void Engine::ManagedWorldRegistry::Unregister(ManagedWorldHandle handle) {

	const WorldSlot* slot = worlds_.TryGet({ handle.index, handle.generation });
	if (!slot) {
		return;
	}

	// 登録を外してから世代を進める
	const auto entry = handles_.find(slot->world);
	if (entry != handles_.end() && entry->second.index == handle.index && entry->second.generation == handle.generation) {
		handles_.erase(entry);
	}
	worlds_.Release({ handle.index, handle.generation });
}

Engine::ECSWorld* Engine::ManagedWorldRegistry::TryResolve(ManagedWorldHandle handle) const {

	const WorldSlot* slot = worlds_.TryGet({ handle.index, handle.generation });
	return slot && slot->lifetime->IsAlive() ? slot->world : nullptr;
}

Engine::ManagedWorldHandle Engine::ManagedWorldRegistry::TryGetHandle(const ECSWorld& world) const {

	const auto it = handles_.find(&world);
	return it != handles_.end() && TryResolve(it->second) == &world ? it->second : ManagedWorldHandle{};
}

bool Engine::ManagedWorldRegistry::IsAlive(ManagedWorldHandle handle) const {

	return TryResolve(handle) != nullptr;
}

Engine::ManagedWorldRegistry& Engine::ManagedWorldRegistry::GetInstance() {

	static ManagedWorldRegistry registry;
	return registry;
}
