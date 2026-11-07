#include "ExponentialHeightFogInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

//============================================================================
//	ExponentialHeightFogInspectorDrawer classMethods
//============================================================================
void Engine::ExponentialHeightFogInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("密度 /m", draft.density, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("高さ減衰 /m", draft.heightFalloff, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("霧色", draft.color);
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("開始距離 m", draft.startDistance, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("最大不透明度", draft.maxOpacity, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("打ち切り距離 m", draft.cutoffDistance, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("太陽方向の散乱色", draft.directionalColor);
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("太陽方向の散乱指数", draft.directionalExponent, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("太陽方向の開始距離 m", draft.directionalStartDistance, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("太陽方向の散乱強度", draft.directionalIntensity, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("空からの色寄与", draft.skyContribution, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("第2層の密度 /m", draft.secondDensity, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("第2層の高さ減衰 /m", draft.secondHeightFalloff, { .dragSpeed = 0.001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("第2層の高さ m", draft.secondHeightOffset, { .dragSpeed = 0.01f, .minValue = -100000.0f });
	});
}
