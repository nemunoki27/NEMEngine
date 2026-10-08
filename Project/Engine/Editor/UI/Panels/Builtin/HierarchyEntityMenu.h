#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine::HierarchyEntityMenu {

	// 選択Entityの操作メニューを表示する
	void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool activeSelf);
}
