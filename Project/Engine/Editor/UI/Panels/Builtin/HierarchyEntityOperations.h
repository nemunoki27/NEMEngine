#pragma once

#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

#include <imgui.h>

namespace Engine::HierarchyEntityOperations {

	// 階層操作の対象と条件を解決する
	bool IsRootEntity(ECSWorld& world, const Entity& entity);
	// 階層操作の対象と条件を解決する
	Entity GetParentEntity(ECSWorld& world, const Entity& entity);
	// 階層操作の対象と条件を解決する
	bool CanReparent(const EditorPanelContext& context, ECSWorld& world, const Entity& child, const Entity& newParent);
	// 階層操作の対象と条件を解決する
	bool CanReorder(const EditorPanelContext& context, ECSWorld& world, const Entity& child, const Entity& anchor);
	// 階層操作の対象と条件を解決する
	Entity ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload);
	// 階層操作の対象と条件を解決する
	std::vector<Entity> ResolveDraggedEntities(const EditorPanelContext& context,
		ECSWorld& world, const ImGuiPayload* payload);
}
