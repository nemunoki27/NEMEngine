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
	// 種別の重複登録を拒否
	if (HasDrawer(type)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"AssetInspectorRegistry: AssetTypeが重複しています type={}", static_cast<int>(type));
		return false;
	}
	// 登録データの所有を移す
	drawers_.emplace_back(std::move(drawer));
	return true;
}

Engine::IAssetInspectorDrawer* Engine::AssetInspectorRegistry::Find(AssetType type) {

	// 編集可能なDrawerを返す
	const std::size_t index = FindIndex(type);
	return index < drawers_.size() ? drawers_[index].get() : nullptr;
}

const Engine::IAssetInspectorDrawer* Engine::AssetInspectorRegistry::Find(AssetType type) const {

	// 読み取り専用のDrawerを返す
	const std::size_t index = FindIndex(type);
	return index < drawers_.size() ? drawers_[index].get() : nullptr;
}

bool Engine::AssetInspectorRegistry::HasDrawer(AssetType type) const {

	// 種別検索を登録済み判定にも使用
	return Find(type) != nullptr;
}

std::size_t Engine::AssetInspectorRegistry::FindIndex(AssetType type) const {

	// 登録済みの種別を一か所で検索
	for (std::size_t index = 0; index < drawers_.size(); ++index) {
		if (drawers_[index]->GetAssetType() == type) {
			return index;
		}
	}
	return drawers_.size();
}
