#include "UIComponentInspectorDrawers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <utility>

//============================================================================
//	UIComponentInspectorDrawers internal
//============================================================================
namespace {

	Engine::ValueEditResult DrawEntityReference(const char* label,
		Engine::ECSWorld& world, Engine::UUID& localFileID) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		std::string preview = "None (Drop entity here)";
		if (localFileID) {
			const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
			if (world.IsAlive(target)) {
				const auto* name = world.TryGetComponent<Engine::NameComponent>(target);
				preview = name ? name->name : "Entity";
			} else {
				preview = "Missing Entity | " + Engine::ToString(localFileID);
			}
		}

		ImGui::PushID(label);
		if (!localFileID) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		}
		ImGui::Button(preview.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
		if (!localFileID) {
			ImGui::PopStyleColor();
		}
		result.anyItemActive = ImGui::IsItemActive();

		if (localFileID && ImGui::BeginPopupContextItem("##deleteEntityReference")) {
			if (ImGui::MenuItem("削除")) {
				localFileID = {};
				result.valueChanged = true;
				result.editFinished = true;
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				Engine::IEditorPanel::kHierarchyDragDropPayloadType)) {
				if (payload->IsDelivery() && payload->DataSize == sizeof(Engine::UUID)) {
					const Engine::UUID stableUUID = *static_cast<const Engine::UUID*>(payload->Data);
					const Engine::Entity target = world.FindByUUID(stableUUID);
					if (world.IsAlive(target)) {
						const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(target);
						if (sceneObject) {
							localFileID = sceneObject->localFileID;
							result.valueChanged = true;
							result.editFinished = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopID();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

	template <class DrawFieldFn>
	void DrawTransitionStyle(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		const char* label, Engine::UITransitionStyle& style, bool& anyItemActive) {

		if (!Engine::MyGUI::CollapsingHeader(label, false)) {
			return;
		}
		drawField(anyItemActive, [&]() { return Engine::MyGUI::ColorEdit("色", style.color); });
		drawField(anyItemActive, [&]() {
			return Engine::MyGUI::DragVector2("スケール", style.scale,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 8.0f });
			});
		drawField(anyItemActive, [&]() {
			return Engine::InspectorDrawerCommon::DrawCheckboxField("テクスチャ上書き", style.overrideTexture);
			});
		if (style.overrideTexture && context.editorContext) {
			drawField(anyItemActive, [&]() {
				return Engine::MyGUI::AssetReferenceField("テクスチャ", style.texture,
					context.editorContext->assetDatabase, { Engine::AssetType::Texture });
				});
		}
	}
}

//========================================================================================================================================================
//	CanvasInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::CanvasInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });

	// 基準解像度は変更不可
	MyGUI::TextVector2("基準解像度", draft.referenceResolution, 1);

	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("ソートレイヤー", draft.sortingLayer); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("描画順", draft.order); });
	if (ImGui::IsItemHovered()) {
		ImGui::BeginTooltip();

		ImGui::Text("キャンバス単位の制御");

		ImGui::EndTooltip();
	}

	ImGui::Indent();
	if (MyGUI::CollapsingHeader("入力設定")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("ゲーム入力をブロック", draft.blockGameplayInput); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("ホバーで選択", draft.mouseHoverSelect); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("ナビゲーションをループ", draft.wrapNavigation); });
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("リピート開始", draft.repeatDelay,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("リピート間隔", draft.repeatInterval,
				{ .dragSpeed = 0.01f,.minValue = 0.01f,.maxValue = 10.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("スティックしきい値", draft.stickThreshold,
				{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f,.flags = ImGuiSliderFlags_AlwaysClamp });
			});
		DrawField(anyItemActive, [&]() { return DrawEntityReference("初期選択", world, draft.firstSelectedLocalFileID); });
	}
	ImGui::Unindent();
}

//========================================================================================================================================================
//	UISelectableInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UISelectableInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("操作可能", draft.interactable); });
	DrawField(anyItemActive, [&]() { return DrawEntityReference("表示対象", world, draft.targetLocalFileID); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("当たり判定を指定", draft.useCustomHitArea); });
	if (draft.useCustomHitArea) {
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("判定オフセット", draft.hitAreaOffset, { .dragSpeed = 0.1f }); });
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector2("判定サイズ", draft.hitAreaSize,
				{ .dragSpeed = 0.1f,.minValue = 0.0f,.maxValue = 100000.0f });
			});
	}
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("遷移時間", draft.transitionDuration,
			{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10.0f });
		});

	if (MyGUI::CollapsingHeader("ナビゲーション")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("方式", draft.navigationMode); });
		if (draft.navigationMode == UINavigationMode::Explicit) {
			DrawField(anyItemActive, [&]() { return DrawEntityReference("上", world, draft.upLocalFileID); });
			DrawField(anyItemActive, [&]() { return DrawEntityReference("下", world, draft.downLocalFileID); });
			DrawField(anyItemActive, [&]() { return DrawEntityReference("左", world, draft.leftLocalFileID); });
			DrawField(anyItemActive, [&]() { return DrawEntityReference("右", world, draft.rightLocalFileID); });
		}
	}
	auto drawField = [&](bool& active, auto&& function) {
		DrawField(active, std::forward<decltype(function)>(function));
	};
	DrawTransitionStyle(drawField, context, "通常", draft.normal, anyItemActive);
	DrawTransitionStyle(drawField, context, "ホバー", draft.highlighted, anyItemActive);
	DrawTransitionStyle(drawField, context, "押下", draft.pressed, anyItemActive);
	DrawTransitionStyle(drawField, context, "選択", draft.selected, anyItemActive);
	DrawTransitionStyle(drawField, context, "無効", draft.disabled, anyItemActive);
}

//========================================================================================================================================================
//	UIButtonInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UIButtonInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::InputText("アクション名", draft.actionName); });
}

//========================================================================================================================================================
//	UIProgressInspectorDrawer classMethods
//========================================================================================================================================================

void Engine::UIProgressInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("最小値", draft.minValue, { .dragSpeed = 0.01f }); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("最大値", draft.maxValue, { .dragSpeed = 0.01f }); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("現在値", draft.value, { .dragSpeed = 0.01f }); });
	DrawField(anyItemActive, [&]() { return DrawEntityReference("フィル対象", world, draft.fillTargetLocalFileID); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("方向", draft.direction); });

	if (MyGUI::CollapsingHeader("補間")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("補間する", draft.smooth); });
		if (draft.smooth) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("補間時間", draft.smoothDuration,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("イージング", draft.smoothEasing); });
		}
	}
	if (MyGUI::CollapsingHeader("遅延表示")) {
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("遅延表示する", draft.delayed); });
		if (draft.delayed) {
			DrawField(anyItemActive, [&]() { return DrawEntityReference("遅延フィル対象", world, draft.delayedTargetLocalFileID); });
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("待機時間", draft.delayedWait,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("遅延時間", draft.delayedDuration,
					{ .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 60.0f });
				});
			DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("遅延イージング", draft.delayedEasing); });
		}
	}
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("非スケール時間", draft.useUnscaledTime); });
}
