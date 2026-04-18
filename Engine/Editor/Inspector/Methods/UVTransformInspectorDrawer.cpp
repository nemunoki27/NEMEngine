#include "UVTransformInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Inspector/Methods/Common/InspectorDrawerCommon.h>

//============================================================================
//	UVTransformInspectorDrawer classMethods
//============================================================================

void Engine::UVTransformInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	SRT編集
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("Position", draft.pos,
				{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("Rotation", draft.rotation,
				{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("Scale", draft.scale,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100000.0f });
			});
	}
}