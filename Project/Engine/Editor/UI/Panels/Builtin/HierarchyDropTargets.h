#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

namespace Engine::HierarchyDropTargets {

	// Assetを親の下かルートに配置する
	void DropProjectAssetToHierarchy(
		const EditorPanelContext& context, ECSWorld& world, const EditorAssetDragDropPayload& payload, const Entity& parent);
	// 兄弟間の並べ替え先を表示する
	void DrawSiblingDropTarget(
		const EditorPanelContext& context, ECSWorld& world, const Entity& anchorEntity, bool insertAfter);
	// ルートへの親子変更とAsset配置先を表示する
	void DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world);
}
