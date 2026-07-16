#include "BehaviorWorld.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>

// c++
#include <algorithm>

//============================================================================
//	BehaviorWorld classMethods
//============================================================================
Engine::BehaviorHandle Engine::BehaviorWorld::Create(uint32_t typeID, const Entity& owner) {

	// インデックスを割り当てる
	BehaviorHandle handle{};
	handle.index = AllocateIndex();

	// レコード情報を初期化する
	BehaviorRecord& record = records_[handle.index];
	const uint32_t generation = record.generation;
	record = BehaviorRecord{};
	record.generation = generation;

	// ビヘイビアの実体を生成する
	const BehaviorTypeInfo& info = BehaviorTypeRegistry::GetInstance().GetInfo(typeID);
	record.instance = info.construct ? info.construct() : nullptr;
	if (!record.instance) {
		free_.emplace_back(handle.index);
		return BehaviorHandle::Null();
	}

	record.alive = true;
	record.owner = owner;
	record.typeID = typeID;

	// ハンドルの世代をレコードの世代と合わせる
	handle.generation = record.generation;
	ownerToRecords_[MakeOwnerKey(owner)].emplace_back(handle.index);
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
	ownerToRecords_.clear();
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

uint32_t Engine::BehaviorWorld::SweepUnseen(ECSWorld& world, const SystemContext& context) {

	uint32_t destroyed = 0;
	// 全てのレコードを走査して生存しているビヘイビアのフラグを確認する
	for (uint32_t i = 0; i < GetRecordCount(); ++i) {

		auto& record = records_[i];
		if (!record.alive) {
			continue;
		}

		// フラグが立っていないビヘイビアは破棄する
		if (!world.IsAlive(record.owner) || !record.seen) {

			DestroyIndex(i, world, context);
			++destroyed;
		}
	}
	return destroyed;
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

uint64_t Engine::BehaviorWorld::MakeOwnerKey(const Entity& owner) {

	return (static_cast<uint64_t>(owner.generation) << 32) | owner.index;
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

	const Entity owner = record.owner;

	// ビヘイビアの状態に応じて適切な関数を呼び出す
	// enabledなものだけOnDisable、一度でもAwake済みのものだけOnDestroyを呼ぶ
	// Awake未実行のinactive scriptはどちらも呼ばず解放だけ行う
	if (record.instance) {
		if (record.enabled) {

			record.instance->OnDisable(world, context, record.owner);
		}
		if (record.awakeCalled) {

			record.instance->OnDestroy(world, context, record.owner);
		}
	}

	// 世代をインクリメントして古いハンドルを無効にする
	const uint32_t generation = record.generation + 1;
	// レコードを初期化して空きIDのスタックに戻す
	record = BehaviorRecord{};
	record.generation = generation;
	free_.emplace_back(index);

	auto ownerIt = ownerToRecords_.find(MakeOwnerKey(owner));
	if (ownerIt != ownerToRecords_.end()) {
		auto& indices = ownerIt->second;
		indices.erase(std::remove(indices.begin(), indices.end(), index), indices.end());
		if (indices.empty()) {
			ownerToRecords_.erase(ownerIt);
		}
	}
}
