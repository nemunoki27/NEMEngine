#include "AssetActionRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <utility>

//============================================================================
//	AssetActionRegistry classMethods
//============================================================================
bool Engine::AssetActionRegistry::Register(AssetActionDescriptor descriptor) {

	if (descriptor.type == AssetType::Unknown) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetActionRegistry: AssetType::Unknownは登録できません");
		return false;
	}
	if (descriptor.displayName.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetActionRegistry: displayNameが空です type={}", static_cast<int>(descriptor.type));
		return false;
	}
	if (HasDescriptor(descriptor.type)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetActionRegistry: AssetTypeが重複しています type={}", static_cast<int>(descriptor.type));
		return false;
	}
	descriptors_.emplace_back(std::move(descriptor));
	return true;
}

const Engine::AssetActionDescriptor* Engine::AssetActionRegistry::Find(AssetType type) const {

	for (const AssetActionDescriptor& descriptor : descriptors_) {
		if (descriptor.type == type) {
			return &descriptor;
		}
	}
	return nullptr;
}

bool Engine::AssetActionRegistry::HasDescriptor(AssetType type) const {

	for (const AssetActionDescriptor& descriptor : descriptors_) {
		if (descriptor.type == type) {
			return true;
		}
	}
	return false;
}
