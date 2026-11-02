#include "ComponentEditorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorPresentation.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <utility>

//============================================================================
//	ComponentEditorRegistry classMethods
//============================================================================
Engine::ComponentEditorRegistry::~ComponentEditorRegistry() = default;

Engine::IInspectorComponentDrawer* Engine::ComponentEditorRegistry::Register(ComponentEditorDescriptor descriptor) {

	// 登録名が無いと識別できないため弾く
	if (descriptor.typeName.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ComponentEditorRegistry: typeNameが空のため登録をスキップしました");
		return nullptr;
	}
	// メニュー表示する登録は表示名と分類が必須
	if (descriptor.showInComponentMenu && (descriptor.menuLabel.empty() || descriptor.category.empty())) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ComponentEditorRegistry: menuLabelまたはcategoryが空です typeName={}", descriptor.typeName);
		return nullptr;
	}
	// Registry上のコンポーネント編集情報はtypeNameで一意にする
	if (HasDescriptor(descriptor.typeName)) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ComponentEditorRegistry: typeNameが重複しています typeName={}", descriptor.typeName);
		return nullptr;
	}

	descriptor.category = GetInspectorComponentCategory(descriptor.typeName, descriptor.category);
	IInspectorComponentDrawer* drawer = nullptr;
	if (descriptor.drawerFactory) {

		std::unique_ptr<IInspectorComponentDrawer> created = descriptor.drawerFactory();
		// 表示名とは分けて編集元をDrawerへ渡す
		if (created) {
			created->SetEditorSourceFile(descriptor.editorSourceFile);
			created->SetDisplayName(descriptor.menuLabel);
			created->SetComponentTypeName(descriptor.typeName);
		}
		drawer = created.get();
		drawers_.emplace_back(std::move(created));
	}
	descriptors_.emplace_back(std::move(descriptor));
	std::stable_sort(descriptors_.begin(), descriptors_.end(), [](const auto& lhs, const auto& rhs) {
		const int left = GetInspectorCategoryOrder(lhs.category), right = GetInspectorCategoryOrder(rhs.category);
		if (left != right) return left < right;
		if (lhs.category != rhs.category) return lhs.category < rhs.category;
		return lhs.menuLabel < rhs.menuLabel;
	});
	return drawer;
}

bool Engine::ComponentEditorRegistry::CanAdd(
	const ComponentEditorDescriptor& descriptor, ECSWorld& world, const Entity& entity) const {

	return descriptor.allowMultiple || !world.HasComponent(entity, descriptor.typeName);
}

std::unique_ptr<Engine::IEditorCommand> Engine::ComponentEditorRegistry::CreateAddCommand(
	const ComponentEditorDescriptor& descriptor, const Entity& entity) const {

	if (!descriptor.addCommandFactory) {
		return nullptr;
	}
	return descriptor.addCommandFactory(entity);
}

bool Engine::ComponentEditorRegistry::HasDescriptor(const std::string& typeName) const {

	for (const ComponentEditorDescriptor& descriptor : descriptors_) {
		if (descriptor.typeName == typeName) {
			return true;
		}
	}
	return false;
}
