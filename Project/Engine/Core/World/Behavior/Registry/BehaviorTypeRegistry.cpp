#include "BehaviorTypeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedBehavior.h>

// c++
#include <filesystem>

//============================================================================
//	BehaviorTypeRegistry classMethods
//============================================================================
uint32_t Engine::BehaviorTypeRegistry::RegisterManaged(const std::string_view& scriptTypeId,
	const std::string_view& fullName, const std::string_view& displayName, const std::string_view& sourcePath) {

	// 既に同じ GUID で登録済みならそのIDを返す（GUID が永続主キー）
	auto existing = guidToID_.find(std::string(scriptTypeId));
	if (existing != guidToID_.end()) {
		return existing->second;
	}

	// 新しい型情報を作成する。construct は Stable GUID で C# instance を生成する
	BehaviorTypeInfo info{};
	info.name = std::string(fullName);
	info.scriptTypeId = std::string(scriptTypeId);
	info.displayName = displayName.empty() ? info.name : std::string(displayName);
	info.sourcePath = std::string(sourcePath);
	info.id = static_cast<uint32_t>(infos_.size());
	info.managed = true;
	info.construct = [id = info.scriptTypeId, name = info.displayName]() -> std::unique_ptr<MonoBehavior> {
		return std::make_unique<ManagedBehavior>(id, name);
		};

	// ClearManagedで空いたスロットを再利用する
	for (auto& slot : infos_) {

		if (slot.managed || !slot.name.empty() || slot.construct) {
			continue;
		}
		info.id = slot.id;
		slot = std::move(info);
		nameToID_[slot.name] = slot.id;
		guidToID_[slot.scriptTypeId] = slot.id;
		return slot.id;
	}

	infos_.emplace_back(info);
	nameToID_[info.name] = info.id;
	guidToID_[info.scriptTypeId] = info.id;
	return info.id;
}

void Engine::BehaviorTypeRegistry::ClearManaged() {

	for (auto& info : infos_) {

		if (!info.managed) {
			continue;
		}

		nameToID_.erase(info.name);
		guidToID_.erase(info.scriptTypeId);
		info.name.clear();
		info.scriptTypeId.clear();
		info.displayName.clear();
		info.sourcePath.clear();
		info.managed = false;
		info.construct = nullptr;
	}
}

const Engine::BehaviorTypeInfo& Engine::BehaviorTypeRegistry::GetInfo(uint32_t id) const {

	assert(id < infos_.size());
	return infos_[id];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindByStableScriptTypeID(const std::string_view& scriptTypeId) const {

	auto it = guidToID_.find(std::string(scriptTypeId));
	if (it == guidToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindByName(const std::string_view& name) const {

	auto it = nameToID_.find(std::string(name));
	if (it == nameToID_.end()) {
		return nullptr;
	}
	return &infos_[it->second];
}

std::vector<const Engine::BehaviorTypeInfo*> Engine::BehaviorTypeRegistry::FindManagedBySourceFile(
	const std::string_view& sourceFilePath) const {

	// 入力とソースパスの「ファイル名」で照合する（base が異なるパス前提でも頑健にする）。
	// 永続識別は GUID 側で行うため、ここは drag&drop の候補抽出だけに使う。
	const std::string inputName = std::filesystem::path(std::string(sourceFilePath)).filename().string();

	std::vector<const BehaviorTypeInfo*> candidates;
	if (inputName.empty()) {
		return candidates;
	}
	for (const BehaviorTypeInfo& info : infos_) {

		if (!info.managed || info.sourcePath.empty()) {
			continue;
		}
		const std::string entryName = std::filesystem::path(info.sourcePath).filename().string();
		if (entryName == inputName) {
			candidates.push_back(&info);
		}
	}
	return candidates;
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

		// 同名クラスが複数名前空間にある場合は曖昧なので、完全名指定を要求する
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
