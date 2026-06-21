#include "ManagedScriptProfilerStore.h"

namespace {

	// typeIDとentityIndexとslotとcallbackを1つの64bit keyへ畳む、entityIndexを含めるためbit詰めではなくFNV-1aで混ぜ衝突は稀で起きてもprofiler上2 entryが混ざるだけ
	uint64_t MakeKey(uint32_t typeID, uint32_t entityIndex, int32_t slot, Engine::ScriptCallbackKind callback) {
		uint64_t hash = 1469598103934665603ull; // FNV offset basis
		const auto mix = [&hash](uint32_t value) {
			for (int i = 0; i < 4; ++i) {
				hash ^= static_cast<uint8_t>(value >> (i * 8));
				hash *= 1099511628211ull; // FNV prime
			}
			};
		mix(typeID);
		mix(entityIndex);
		mix(static_cast<uint32_t>(slot));
		mix(static_cast<uint32_t>(callback));
		return hash;
	}
}

void Engine::ManagedScriptProfilerStore::Record([[maybe_unused]] uint32_t typeID, [[maybe_unused]] uint32_t entityIndex,
	[[maybe_unused]] int32_t slot, [[maybe_unused]] ScriptCallbackKind callback,
	[[maybe_unused]] float milliseconds, [[maybe_unused]] bool threwException) {

	// detail無効構成ではbodyごと消える、if constexprで空関数化しunreachable警告も出さない
	if constexpr (kDetailEnabled) {

		// 自身の計測overheadを測る、store更新に要した時間をaccumulateして報告する
		const auto overheadStart = std::chrono::high_resolution_clock::now();

		const uint64_t key = MakeKey(typeID, entityIndex, slot, callback);
		auto it = keyToIndex_.find(key);
		if (it == keyToIndex_.end()) {

			// 上限超過時は新規キーを足さず既存キーの更新だけ続ける
			if (entries_.size() >= kMaxEntries) {
				capped_ = true;
			}
			else {
				const size_t index = entries_.size();
				ManagedScriptProfileEntry entry{};
				entry.typeID = typeID;
				entry.entityIndex = entityIndex;
				entry.slot = slot;
				entry.callback = callback;
				entries_.push_back(entry);
				it = keyToIndex_.emplace(key, index).first;
			}
		}

		if (it != keyToIndex_.end()) {
			ManagedScriptProfileEntry& entry = entries_[it->second];
			const double ms = static_cast<double>(milliseconds);
			entry.totalMs += ms;
			entry.maxMs = ms > entry.maxMs ? ms : entry.maxMs;
			++entry.callCount;
			if (threwException) {
				++entry.exceptionCount;
			}
			++version_;
		}

		const std::chrono::duration<double, std::milli> overhead =
			std::chrono::high_resolution_clock::now() - overheadStart;
		overheadMs_ += overhead.count();
	}
}

void Engine::ManagedScriptProfilerStore::RecordCoroutineResume([[maybe_unused]] float milliseconds) {

	if constexpr (kDetailEnabled) {
		coroutineResumeMs_ += static_cast<double>(milliseconds);
	}
}

void Engine::ManagedScriptProfilerStore::Reset() {

	keyToIndex_.clear();
	entries_.clear();
	coroutineResumeMs_ = 0.0;
	overheadMs_ = 0.0;
	capped_ = false;
	++version_;
}

Engine::ManagedScriptProfilerStore& Engine::ManagedScriptProfilerStore::GetInstance() {

	static ManagedScriptProfilerStore store;
	return store;
}
