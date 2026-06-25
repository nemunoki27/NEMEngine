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

	// 空き枠を再利用するか、新しい枠を確保する
	uint32_t index = 0;
	if (!free_.empty()) {

		index = free_.back();
		free_.pop_back();
	} else {

		index = static_cast<uint32_t>(slots_.size());
		slots_.emplace_back(Slot{});
	}

	Slot& slot = slots_[index];
	slot.world = &world;
	slot.inUse = true;

	ManagedWorldHandle handle{};
	handle.index = index;
	handle.generation = slot.generation;
	return handle;
}

void Engine::ManagedWorldRegistry::Unregister(ManagedWorldHandle handle) {

	// 既に解除済み/古いハンドルは無視する
	if (handle.index == kInvalidIndex || slots_.size() <= handle.index) {
		return;
	}
	Slot& slot = slots_[handle.index];
	if (!slot.inUse || slot.generation != handle.generation) {
		return;
	}

	// 枠を解放し、generationを進めて古いハンドルを無効化する
	slot.world = nullptr;
	slot.inUse = false;
	// 0はゼロ初期化Entityと衝突するためwrap時は1へ飛ばす
	if (++slot.generation == 0) { slot.generation = 1; }
	free_.emplace_back(handle.index);
}

Engine::ECSWorld* Engine::ManagedWorldRegistry::TryResolve(ManagedWorldHandle handle) const {

	// 範囲と使用中と世代一致を確認する、古いハンドルはnullptr
	if (handle.index == kInvalidIndex || slots_.size() <= handle.index) {
		return nullptr;
	}
	const Slot& slot = slots_[handle.index];
	if (!slot.inUse || slot.generation != handle.generation) {
		return nullptr;
	}
	return slot.world;
}

Engine::ManagedWorldHandle Engine::ManagedWorldRegistry::TryGetHandle(const ECSWorld& world) const {

	// 登録枠は少数なので線形探索で十分、worldごとに1枠
	for (uint32_t i = 0; i < slots_.size(); ++i) {

		const Slot& slot = slots_[i];
		if (slot.inUse && slot.world == &world) {

			ManagedWorldHandle handle{};
			handle.index = i;
			handle.generation = slot.generation;
			return handle;
		}
	}
	return ManagedWorldHandle{};
}

bool Engine::ManagedWorldRegistry::IsAlive(ManagedWorldHandle handle) const {

	return TryResolve(handle) != nullptr;
}

Engine::ManagedWorldRegistry& Engine::ManagedWorldRegistry::GetInstance() {

	static ManagedWorldRegistry registry;
	return registry;
}
