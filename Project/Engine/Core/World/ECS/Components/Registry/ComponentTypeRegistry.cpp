#include "ComponentTypeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Generated/BuiltinComponentRegistry.generated.h>

//============================================================================
//	ComponentTypeRegistry classMethods
//============================================================================
Engine::ComponentTypeRegistry::ComponentTypeRegistry() {

	// 登録後の型情報をChunkから参照し続ける
	infos_.reserve(kMaxComponentTypes);
	RegisterBuiltinComponents(*this);
}

const Engine::ComponentTypeInfo& Engine::ComponentTypeRegistry::GetInfo(uint32_t id) const {

	if (id >= infos_.size()) {
		throw std::out_of_range("未登録のComponentType IDです");
	}
	return infos_[id];
}

const Engine::ComponentTypeInfo* Engine::ComponentTypeRegistry::FindByName(const std::string_view& name) const {

	auto it = nameToID_.find(std::string(name));
	if (it == nameToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

Engine::ComponentTypeRegistry& Engine::ComponentTypeRegistry::GetInstance() {

	static ComponentTypeRegistry registry;
	return registry;
}

void Engine::ComponentTypeRegistry::RegisterInfo(ComponentTypeInfo info, const void* typeKey) {

	if (!info.nothrowMoveConstructible) {
		throw std::invalid_argument("Chunkへ格納するComponentは例外なしで移動できる必要があります");
	}
	if (info.id != infos_.size() || info.id >= kMaxComponentTypes || info.name.empty() ||
		nameToID_.contains(info.name) || typeKeyToID_.contains(typeKey)) {
		throw std::invalid_argument("Componentの固定ID、型名またはC++型が不正です");
	}

	// 検索表が揃うまで型情報を公開しない
	auto nameEntry = nameToID_.emplace(info.name, info.id).first;
	try {
		typeKeyToID_.emplace(typeKey, info.id);
	} catch (...) {
		nameToID_.erase(nameEntry);
		throw;
	}
	static_assert(std::is_nothrow_move_constructible_v<ComponentTypeInfo>);
	infos_.emplace_back(std::move(info));
}
