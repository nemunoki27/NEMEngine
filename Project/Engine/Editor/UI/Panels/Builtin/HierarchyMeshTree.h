#pragma once

#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

#include <functional>

namespace Engine::HierarchyMeshTree {

	using DrawEntityCallback = std::function<void(const Entity&)>;

	// SubMeshの一覧と選択を描画する
	void DrawSubMeshNodes(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
	// Joint階層と親子付けEntityを描画する
	void DrawSkinnedMeshNodes(const EditorPanelContext& context, ECSWorld& world, const Entity& entity,
		const DrawEntityCallback& drawEntity);
}
