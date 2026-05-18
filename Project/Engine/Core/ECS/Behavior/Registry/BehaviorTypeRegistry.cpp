#include "BehaviorTypeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/ManagedBehavior.h>

//============================================================================
//	BehaviorTypeRegistry classMethods
//============================================================================

uint32_t Engine::BehaviorTypeRegistry::RegisterManaged(const std::string_view& name) {

	// 既に登録済みならそのIDを返す
	auto it = nameToID_.find(std::string(name));
	if (it != nameToID_.end()) {
		return it->second;
	}

	// 新しい型情報を作成して登録
	BehaviorTypeInfo info{};
	info.name = std::string(name);
	info.id = static_cast<uint32_t>(infos_.size());
	info.managed = true;
	info.construct = [typeName = info.name]() -> std::unique_ptr<MonoBehavior> {
		return std::make_unique<ManagedBehavior>(typeName);
		};

	for (auto& slot : infos_) {

		if (!slot.name.empty() || slot.construct) {
			continue;
		}
		info.id = slot.id;
		slot = std::move(info);
		nameToID_[slot.name] = slot.id;
		return slot.id;
	}

	infos_.emplace_back(info);
	nameToID_[info.name] = info.id;
	return info.id;
}

void Engine::BehaviorTypeRegistry::ClearManaged() {

	for (auto& info : infos_) {

		if (!info.managed) {
			continue;
		}

		nameToID_.erase(info.name);
		info.name.clear();
		info.managed = false;
		info.construct = nullptr;
	}
}

const Engine::BehaviorTypeInfo& Engine::BehaviorTypeRegistry::GetInfo(uint32_t id) const {

	assert(id < infos_.size());
	return infos_[id];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindByName(const std::string_view& name) const {

	auto it = nameToID_.find(std::string(name));
	if (it == nameToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindManagedBySimpleName(const std::string_view& name) const {

	const BehaviorTypeInfo* result = nullptr;
	for (const BehaviorTypeInfo& info : infos_) {

		if (!info.managed || info.name.empty()) {
			continue;
		}

		const size_t dot = info.name.find_last_of('.');
		const std::string_view simpleName = dot == std::string::npos ?
			std::string_view(info.name) : std::string_view(info.name).substr(dot + 1);
		if (simpleName != name) {
			continue;
		}

		// 同名クラスが複数名前空間にある場合は曖昧なので、完全名指定を要求する。
		if (result) {
			return nullptr;
		}
		result = &info;
	}
	return result;
}

Engine::BehaviorTypeRegistry& Engine::BehaviorTypeRegistry::GetInstance() {

	static BehaviorTypeRegistry registry;
	return registry;
}
