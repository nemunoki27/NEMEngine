#include "LightInspectorDrawers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

//============================================================================
//	DirectionalLightInspectorDrawer classMethods
//============================================================================
void Engine::DirectionalLightInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector3("方向", draft.direction,
			{ .dragSpeed = 0.01f,.minValue = -1.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("強度", draft.intensity,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("影の強さ", draft.shadowStrength,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("光源角度", draft.shadowAngularRadius,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 5.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "レイヤーマスク", draft.affectLayerMask);
		});
}

void Engine::DirectionalLightInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const DirectionalLightComponent& beforeComponent,
	DirectionalLightComponent& afterComponent) {

	afterComponent.direction = Vector3::Normalize(afterComponent.direction);
}

//============================================================================
//	PointLightInspectorDrawer classMethods
//============================================================================
void Engine::PointLightInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("強度", draft.intensity,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("半径", draft.radius,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("減衰", draft.decay,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("影の強さ", draft.shadowStrength,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("光源半径", draft.shadowRadius,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 32.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "レイヤーマスク", draft.affectLayerMask);
	});
}

//============================================================================
//	RectLightInspectorDrawer classMethods
//============================================================================
void Engine::RectLightInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("強度", draft.intensity,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("減衰半径", draft.attenuationRadius,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1024.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("光源幅", draft.sourceWidth,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("光源高さ", draft.sourceHeight,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("減衰", draft.decay,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("バーンドア角度", draft.barnDoorAngle,
			{ .dragSpeed = 0.1f,.minValue = 0.0f,.maxValue = 89.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("バーンドア長さ", draft.barnDoorLength,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("影の強さ", draft.shadowStrength,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context,
			"レイヤーマスク", draft.affectLayerMask);
		});
}

//============================================================================
//	SpotLightInspectorDrawer classMethods
//============================================================================
void Engine::SpotLightInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector3("方向", draft.direction,
			{ .dragSpeed = 0.01f,.minValue = -1.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("強度", draft.intensity,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 128.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("距離", draft.distance,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1024.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("減衰", draft.decay,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 512.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("角度余弦", draft.cosAngle,
			{ .dragSpeed = 0.01f,.minValue = -Math::pi,.maxValue = Math::pi });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("減衰開始余弦", draft.cosFalloffStart,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 8.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("影の強さ", draft.shadowStrength,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("光源半径", draft.shadowRadius,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 32.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawLayerMaskField(context, "レイヤーマスク", draft.affectLayerMask);
		});
}

void Engine::SpotLightInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const SpotLightComponent& beforeComponent,
	SpotLightComponent& afterComponent) {

	afterComponent.direction = Vector3::Normalize(afterComponent.direction);
}
