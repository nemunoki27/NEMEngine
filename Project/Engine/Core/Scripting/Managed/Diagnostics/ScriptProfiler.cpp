#include "ScriptProfiler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>

// c++
#include <algorithm>
#include <utility>

namespace Engine {

	ScriptProfileSnapshot ScriptProfiler::Capture() const {

		ScriptProfileSnapshot snapshot{ rows_, frameCount_, LastFrame(), overflowed_ };
		for (auto& row : snapshot.rows) {
			row.current = row.history[snapshot.lastFrame];
		}
		return snapshot;
	}

	ScriptProfiler& ScriptProfiler::GetInstance() {
		static ScriptProfiler profiler;
		return profiler;
	}

	uint64_t ScriptProfiler::OwnerID(ManagedScriptInstanceHandle handle) {
		return (static_cast<uint64_t>(handle.generation) << 32) | handle.index;
	}

	void ScriptProfiler::Register(ScriptProfileOwner owner) {
		entityOwners_[{ owner.entity.world.index, owner.entity.world.generation,
			owner.entity.index, owner.entity.generation, owner.slotID }] = owner.id;
		owners_.insert_or_assign(owner.id, std::move(owner));
	}

	void ScriptProfiler::Unregister(ManagedScriptInstanceHandle handle) {
		const uint64_t id = OwnerID(handle);
		if (auto it = owners_.find(id); it != owners_.end()) {
			const auto& owner = it->second;
			entityOwners_.erase({ owner.entity.world.index, owner.entity.world.generation,
				owner.entity.index, owner.entity.generation, owner.slotID });
		}
		owners_.erase(id);
		if (detailOwner_ == id) {
			Configure(enabled_, {}, 0);
		}
	}

	void ScriptProfiler::ResetOwners() {
		owners_.clear();
		entityOwners_.clear();
		Configure(enabled_, {}, 0);
		Clear();
	}

	void ScriptProfiler::Configure(bool enabled, std::string typeName, uint64_t ownerID) {
		const bool keepCapture = enabled_ && !enabled && typeName == detailType_ && ownerID == detailOwner_;
		enabled_ = enabled;
		detailType_ = std::move(typeName);
		detailOwner_ = ownerID;
		if (keepCapture) {
			callbackStack_.clear();
			detailStack_.clear();
		} else {
			Clear();
		}
		callbackStack_.reserve(128);
		detailStack_.reserve(128);
		NotifySelection();
	}

	void ScriptProfiler::NotifySelection() {
		ManagedNativeEntity entity{};
		uint64_t slotID = 0;
		if (auto it = owners_.find(detailOwner_); it != owners_.end()) {
			entity = it->second.entity;
			slotID = it->second.slotID;
		}
		ManagedScriptRuntime::GetInstance().ConfigureProfiler(
			enabled_ ? detailType_.c_str() : "", entity, slotID);
	}

	void ScriptProfiler::Clear() {
		rows_.clear();
		rowLookup_.clear();
		callbackStack_.clear();
		detailStack_.clear();
		frameIndex_ = 0;
		frameCount_ = 0;
		overflowed_ = false;
	}

	void ScriptProfiler::BeginFrame() {
		if (!enabled_) {
			return;
		}
		for (auto& row : rows_) {
			row.current = {};
		}
	}

	void ScriptProfiler::EndFrame() {
		if (!enabled_) {
			return;
		}
		// yieldやawaitを跨いだ区間は次フレームへ持ち越さない
		callbackStack_.clear();
		detailStack_.clear();
		for (auto& row : rows_) {
			row.history[frameIndex_] = row.current;
		}
		frameIndex_ = (frameIndex_ + 1) % 300;
		frameCount_ = std::min(frameCount_ + 1, 300u);
	}

	int32_t ScriptProfiler::FindRow(const ScriptProfileOwner& owner, const char* name, bool detail, bool grouped, int32_t parent) {
		const auto key = std::tuple{ detail, grouped ? uint64_t{0} : owner.id,
			std::string_view(owner.typeID), parent, std::string_view(name) };
		if (auto it = rowLookup_.find(key); it != rowLookup_.end()) {
			return it->second;
		}
		if (rows_.size() >= 4096) {
			overflowed_ = true;
			return -1;
		}
		const int32_t index = static_cast<int32_t>(rows_.size());
		ScriptProfileRow row{};
		row.owner = owner;
		row.name = name;
		row.parent = parent;
		row.detail = detail;
		row.grouped = grouped;
		rows_.push_back(std::move(row));
		rowLookup_.emplace(RowKey{ detail, grouped ? 0 : owner.id, owner.typeID, parent, name }, index);
		return index;
	}

	uint64_t ScriptProfiler::BeginCallback(ManagedScriptInstanceHandle handle, const char* name) {
		if (!enabled_) {
			return 0;
		}
		const auto it = owners_.find(OwnerID(handle));
		return it != owners_.end() ? Begin(it->second, name, false) : 0;
	}

	uint64_t ScriptProfiler::BeginDetail(ManagedNativeEntity entity, uint64_t slotID, const char* name) {
		if (!enabled_ || detailType_.empty() || !name || !name[0]) {
			return 0;
		}
		const auto found = entityOwners_.find({ entity.world.index, entity.world.generation,
			entity.index, entity.generation, slotID });
		if (found != entityOwners_.end()) {
			const auto& owner = owners_.at(found->second);
			if ((detailOwner_ == 0 || detailOwner_ == owner.id) && owner.typeName == detailType_) {
				return Begin(owner, name, true);
			}
		}
		return 0;
	}

	uint64_t ScriptProfiler::Begin(const ScriptProfileOwner& owner, const char* name, bool detail) {
		auto& stack = detail ? detailStack_ : callbackStack_;
		if (stack.size() >= 128) {
			overflowed_ = true;
			return 0;
		}
		const int32_t parent = detail && !stack.empty() ? stack.back().row : -1;
		const int32_t row = FindRow(owner, name, detail, true, parent);
		if (row < 0) {
			return 0;
		}
		const int32_t instanceRow = detail ? -1 : FindRow(owner, name, false, false, -1);
		const uint64_t token = nextToken_++;
		stack.push_back({ token, row, instanceRow, Clock::now(), 0 });
		return token;
	}

	void ScriptProfiler::End(uint64_t token, bool detail) {
		if (!token) {
			return;
		}
		auto& stack = detail ? detailStack_ : callbackStack_;
		if (stack.empty() || stack.back().token != token) {
			return;
		}
		const Sample sample = stack.back();
		const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - sample.start).count();
		stack.pop_back();
		if (!stack.empty()) {
			stack.back().childrenMs += elapsed;
		}
		for (int32_t index : { sample.row, sample.instanceRow }) {
			if (index >= 0) {
				auto& value = rows_[index].current;
				value.inclusiveMs += elapsed;
				value.selfMs += std::max(0.0, elapsed - sample.childrenMs);
				++value.calls;
			}
		}
	}
}
