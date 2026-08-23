#include "BehaviorTypeRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Scripting/Managed/ManagedBehavior.h>

// c++
#include <filesystem>

//============================================================================
//	BehaviorTypeRegistry classMethods
//============================================================================
uint32_t Engine::BehaviorTypeRegistry::RegisterManaged(const std::string_view& scriptTypeID,
	const std::string_view& fullName, const std::string_view& displayName, const std::string_view& sourcePath,
	int32_t defaultExecutionOrder) {

	// 既に同じGUIDで登録済みならそのIDを返す、GUIDが永続主キー
	auto existing = guidToID_.find(std::string(scriptTypeID));
	if (existing != guidToID_.end()) {
		// 既存登録でもdefault orderは最新を反映する、reloadでattribute値が変わり得る
		infos_[existing->second].defaultExecutionOrder = defaultExecutionOrder;
		return existing->second;
	}

	// 新しい型情報を作成する、constructはStable GUIDでC# instanceを生成する
	BehaviorTypeInfo info{};
	info.name = std::string(fullName);
	info.scriptTypeID = std::string(scriptTypeID);
	info.displayName = displayName.empty() ? info.name : std::string(displayName);
	info.sourcePath = std::string(sourcePath);
	info.defaultExecutionOrder = defaultExecutionOrder;
	info.id = static_cast<uint32_t>(infos_.size());
	info.managed = true;
	info.construct = [id = info.scriptTypeID, name = info.displayName]() -> std::unique_ptr<MonoBehavior> {
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
		guidToID_[slot.scriptTypeID] = slot.id;
		return slot.id;
	}

	infos_.emplace_back(info);
	nameToID_[info.name] = info.id;
	guidToID_[info.scriptTypeID] = info.id;
	return info.id;
}

void Engine::BehaviorTypeRegistry::ClearManaged() {

	for (auto& info : infos_) {

		if (!info.managed) {
			continue;
		}

		nameToID_.erase(info.name);
		guidToID_.erase(info.scriptTypeID);
		info.name.clear();
		info.scriptTypeID.clear();
		info.displayName.clear();
		info.sourcePath.clear();
		info.managed = false;
		info.construct = nullptr;
	}
}

const Engine::BehaviorTypeInfo& Engine::BehaviorTypeRegistry::GetInfo(uint32_t id) const {

	Assert::Call(id < infos_.size(), "未登録のBehaviorType IDです");
	return infos_[id];
}

const Engine::BehaviorTypeInfo* Engine::BehaviorTypeRegistry::FindByStableScriptTypeID(const std::string_view& scriptTypeID) const {

	auto it = guidToID_.find(std::string(scriptTypeID));
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

	// 入力とソースパスのファイル名で照合しbaseが異なるパスでも頑健にする、永続識別はGUID側で行うためここはdrag&dropの候補抽出だけに使う
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

Engine::BehaviorTypeRegistry& Engine::BehaviorTypeRegistry::GetInstance() {

	static BehaviorTypeRegistry registry;
	return registry;
}
