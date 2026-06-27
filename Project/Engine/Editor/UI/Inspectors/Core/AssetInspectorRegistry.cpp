#include "AssetInspectorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <utility>

//============================================================================
//	AssetInspectorRegistry classMethods
//============================================================================
Engine::AssetInspectorRegistry::~AssetInspectorRegistry() = default;

bool Engine::AssetInspectorRegistry::Register(std::unique_ptr<IAssetInspectorDrawer> drawer) {

	if (!drawer) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetInspectorRegistry: drawerがnullのため登録をスキップしました");
		return false;
	}
	const AssetType type = drawer->GetAssetType();
	if (type == AssetType::Unknown) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetInspectorRegistry: AssetType::Unknownは登録できません");
		return false;
	}
	if (HasDrawer(type)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetInspectorRegistry: AssetTypeが重複しています type={}", static_cast<int>(type));
		return false;
	}
	drawers_.emplace_back(std::move(drawer));
	return true;
}

Engine::IAssetInspectorDrawer* Engine::AssetInspectorRegistry::Find(AssetType type) const {

	for (const std::unique_ptr<IAssetInspectorDrawer>& drawer : drawers_) {
		if (drawer->GetAssetType() == type) {
			return drawer.get();
		}
	}
	return nullptr;
}

bool Engine::AssetInspectorRegistry::HasDrawer(AssetType type) const {

	for (const std::unique_ptr<IAssetInspectorDrawer>& drawer : drawers_) {
		if (drawer->GetAssetType() == type) {
			return true;
		}
	}
	return false;
}
