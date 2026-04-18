#include "BehaviorWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>

//============================================================================
//	BehaviorWorld classMethods
//============================================================================

Engine::BehaviorHandle Engine::BehaviorWorld::Create(uint32_t typeID, const Entity& owner) {

	// インデックスを割り当てる
	BehaviorHandle handle{};
	handle.index = AllocateIndex();

	// レコード情報を初期化する
	BehaviorRecord& record = records_[handle.index];
	record.alive = true;
	record.owner = owner;
	record.enabled = false;
	record.awakeCalled = false;
	record.startCalled = false;
	record.seen = false;
	record.typeID = typeID;

	// ハンドルの世代をレコードの世代と合わせる
	handle.generation = record.generation;

	// ビヘイビアの実体を生成する
	const BehaviorTypeInfo& info = BehaviorTypeRegistry::GetInstance().GetInfo(typeID);
	record.instance = info.construct ? info.construct() : nullptr;
	return handle;
}

void Engine::BehaviorWorld::Destroy(const BehaviorHandle& handle, ECSWorld& world, const SystemContext& context) {

	if (!IsAlive(handle)) {
		return;
	}
	DestroyIndex(handle.index, world, context);
}

void Engine::BehaviorWorld::DestroyAll(ECSWorld& world, const SystemContext& context) {

	// 全てのレコードを走査して生存しているビヘイビアを破棄する
	for (uint32_t i = 0; i < GetRecordCount(); ++i) {
		if (records_[i].alive) {

			DestroyIndex(i, world, context);
		}
	}
}

void Engine::BehaviorWorld::ClearSeenFlags() {

	// 全てのレコードを走査して生存しているビヘイビアのフラグをクリアする
	for (auto& record : records_) {
		if (!record.alive) {
			continue;
		}
		record.seen = false;
	}
}

void Engine::BehaviorWorld::SweepUnseen(ECSWorld& world, const SystemContext& context) {

	// 全てのレコードを走査して生存しているビヘイビアのフラグを確認する
	for (uint32_t i = 0; i < GetRecordCount(); ++i) {

		auto& record = records_[i];
		if (!record.alive) {
			continue;
		}

		// フラグが立っていないビヘイビアは破棄する
		if (!world.IsAlive(record.owner) || !record.seen) {

			DestroyIndex(i, world, context);
		}
	}
}

bool Engine::BehaviorWorld::IsAlive(const BehaviorHandle& handle) const {

	if (!handle.IsValid() || GetRecordCount() <= handle.index) {
		return false;
	}
	const BehaviorRecord& record = records_[handle.index];
	return record.alive && record.generation == handle.generation;
}

Engine::BehaviorRecord* Engine::BehaviorWorld::GetRecord(const BehaviorHandle& handle) {

	if (!IsAlive(handle)) {
		return nullptr;
	}
	return &records_[handle.index];
}

const Engine::BehaviorRecord* Engine::BehaviorWorld::GetRecord(const BehaviorHandle& handle) const {

	if (!IsAlive(handle)) {
		return nullptr;
	}
	return &records_[handle.index];
}

Engine::MonoBehavior* Engine::BehaviorWorld::GetBehavior(const BehaviorHandle& handle) {

	auto* record = GetRecord(handle);
	return record ? record->instance.get() : nullptr;
}

uint32_t Engine::BehaviorWorld::AllocateIndex() {

	// 空きIDのスタックから割り当てる
	if (!free_.empty()) {

		uint32_t index = free_.back();
		free_.pop_back();
		return index;
	}
	// 空きIDがない場合は新しいIDを割り当てる
	records_.emplace_back(BehaviorRecord{});
	return GetRecordCount() - 1;
}

void Engine::BehaviorWorld::DestroyIndex(uint32_t index, ECSWorld& world, const SystemContext& context) {

	// インデックスが有効か確認する
	if (GetRecordCount() <= index) {
		return;
	}

	BehaviorRecord& record = records_[index];
	if (!record.alive) {
		return;
	}

	// ビヘイビアの状態に応じて適切な関数を呼び出す
	if (record.instance) {
		if (record.enabled) {

			record.instance->OnDisable(world, context, record.owner);
		}
		record.instance->OnDestroy(world, context, record.owner);
	}

	// レコードを初期化して空きIDのスタックに戻す
	record.instance.reset();
	record.owner = Entity::Null();
	record.enabled = false;
	record.awakeCalled = false;
	record.startCalled = false;
	record.seen = false;
	record.alive = false;
	// 世代をインクリメントして古いハンドルを無効にする
	++record.generation;
	free_.emplace_back(index);
}