#include "ReflectionProbeInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

//============================================================================
//	ReflectionProbeInspectorDrawer classMethods
//============================================================================
void Engine::ReflectionProbeInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() {
		bool box = draft.shape == 1u;
		const bool changed = MyGUI::Checkbox("Box形状", box);
		if (changed) draft.shape = box ? 1u : 0u;
		return ValueEditResult{.valueChanged = changed, .editFinished = changed};
	});
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("半径", draft.radius, {.minValue = 0.01f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("Box半幅", draft.extents, {.minValue = 0.01f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("強度", draft.intensity, {.minValue = 0.0f}); });
}
