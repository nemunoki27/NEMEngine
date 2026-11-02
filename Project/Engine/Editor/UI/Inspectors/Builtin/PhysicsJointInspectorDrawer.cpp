#include "PhysicsJointInspectorDrawer.h"
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>
#include <string>

#include <imgui.h>

namespace {

	// Jointの接続先を編集
	Engine::ValueEditResult DrawConnectedBody(Engine::ECSWorld& world, Engine::UUID& localFileID) {

		return Engine::SceneEntityReferenceFields::DrawLocal("接続先", world, localFileID);
	}

}

//============================================================================
//	FixedJointInspectorDrawer classMethods
//============================================================================
void Engine::FixedJointInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context, ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	PushEditResult(DrawConnectedBody(world, draft.connectedBodyLocalFileID), anyItemActive);
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector3("支点", draft.anchor, { .dragSpeed = 0.01f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"接続先支点を自動設定", draft.autoConfigureConnectedAnchor);
		});
	if (!draft.autoConfigureConnectedAnchor) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3(
				"接続先支点", draft.connectedAnchor, { .dragSpeed = 0.01f });
			});
	}
}

//============================================================================
//	HingeJointInspectorDrawer classMethods
//============================================================================
void Engine::HingeJointInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context, ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	PushEditResult(DrawConnectedBody(world, draft.connectedBodyLocalFileID), anyItemActive);
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector3("支点", draft.anchor, { .dragSpeed = 0.01f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField(
			"接続先支点を自動設定", draft.autoConfigureConnectedAnchor);
		});
	if (!draft.autoConfigureConnectedAnchor) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3(
				"接続先支点", draft.connectedAnchor, { .dragSpeed = 0.01f });
			});
	}
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragVector3("回転軸", draft.axis, { .dragSpeed = 0.01f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("角度制限", draft.useLimits);
		});
	if (draft.useLimits) {
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("最小角度", draft.minAngle, { .dragSpeed = 0.1f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("最大角度", draft.maxAngle, { .dragSpeed = 0.1f });
			});
	}
}

void Engine::HingeJointInspectorDrawer::OnBeforeCommit(
	[[maybe_unused]] const HingeJointComponent& beforeComponent,
	HingeJointComponent& afterComponent) {

	if (afterComponent.maxAngle < afterComponent.minAngle) {
		std::swap(afterComponent.minAngle, afterComponent.maxAngle);
	}
	if (afterComponent.axis.Length() <= 0.0001f) {
		afterComponent.axis = Vector3(0.0f, 1.0f, 0.0f);
	} else {
		afterComponent.axis = Vector3::Normalize(afterComponent.axis);
	}
}
