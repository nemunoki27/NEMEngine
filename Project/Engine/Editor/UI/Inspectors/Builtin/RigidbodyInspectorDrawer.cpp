#include "RigidbodyInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	RigidbodyInspectorDrawer classMethods
//============================================================================

void Engine::RigidbodyInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	//============================================================================
	//	運動設定
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("種別", draft.bodyType);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("質量", draft.mass,
				{ .dragSpeed = 0.01f, .minValue = 0.0f });
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("重力を使う", draft.useGravity);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("重力倍率", draft.gravityScale,
				{ .dragSpeed = 0.01f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("速度減衰", draft.linearDamping,
				{ .dragSpeed = 0.01f, .minValue = 0.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("はね返り", draft.restitution,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("摩擦", draft.friction,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("角速度減衰", draft.angularDamping,
				{ .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3("初速度", draft.linearVelocity,
				{ .dragSpeed = 0.01f });
			});
	}

	//============================================================================
	//	移動拘束
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("X位置を固定", draft.freezePositionX);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Y位置を固定", draft.freezePositionY);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("Z位置を固定", draft.freezePositionZ);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("支えが外れたら倒れる", draft.allowTopple);
			});
	}
}
