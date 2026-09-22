#pragma once

#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>

namespace Engine::EntityCreationMenu {

	// Entityプリセットの作成メニューを描画する
	void DrawEntityCreationMenu(const EditorPanelContext& context, UUID parentStableUUID,
		const char* label, Dimension defaultDimension);
}
