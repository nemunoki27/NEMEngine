#include "PhysicsJointInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <algorithm>
#include <string>

#include <imgui.h>

namespace {

	// Entity参照の表示名を作成
	std::string MakeEntityLabel(Engine::ECSWorld& world, Engine::UUID localFileID) {

		if (!localFileID) {
			return "なし";
		}
		const Engine::Entity entity = Engine::SceneObjectUtility::FindByLocalFileID(
			world, localFileID);
		if (!entity.IsValid() || !world.IsAlive(entity)) {
			return "不明 : " + Engine::ToString(localFileID);
		}
		if (const auto* name = world.TryGetComponent<Engine::NameComponent>(entity)) {
			return name->name;
		}
		return Engine::ToString(localFileID);
	}

	// Jointの接続先を編集
	Engine::ValueEditResult DrawConnectedBody(Engine::ECSWorld& world,
		Engine::UUID& localFileID) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow("接続先")) {
			return result;
		}

		constexpr float kClearButtonWidth = 56.0f;
		const std::string label = MakeEntityLabel(world, localFileID);
		ImGui::Button(label.c_str(), ImVec2((std::max)(0.0f,
			ImGui::GetContentRegionAvail().x - kClearButtonWidth), 0.0f));
		result.anyItemActive |= ImGui::IsItemActive();
		if (ImGui::BeginDragDropTarget()) {
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				Engine::IEditorPanel::kHierarchyDragDropPayloadType);
			if (payload && payload->DataSize == sizeof(Engine::UUID)) {
				const Engine::Entity target = world.FindByUUID(
					*static_cast<const Engine::UUID*>(payload->Data));
				if (const auto* sceneObject =
					world.TryGetComponent<Engine::SceneObjectComponent>(target)) {
					localFileID = sceneObject->localFileID;
					result.valueChanged = true;
					result.editFinished = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();
		if (ImGui::Button("クリア", ImVec2(kClearButtonWidth, 0.0f))) {
			localFileID = {};
			result.valueChanged = true;
			result.editFinished = true;
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
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
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
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
		return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled);
		});
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
