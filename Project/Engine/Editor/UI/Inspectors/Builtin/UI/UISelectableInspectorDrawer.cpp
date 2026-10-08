#include "UISelectableInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/Core/EditorContext.h>

// c++
#include <utility>

//============================================================================
//	UISelectableInspectorDrawer classMethods
//============================================================================

//============================================================================
//	UISelectableInspectorDrawer internal
//============================================================================

namespace {

	template <class DrawFieldFn>
	void DrawTransitionAnimationFields(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		drawField(anyItemActive,
			[&]() { return Engine::InspectorDrawerCommon::DrawCheckboxField("アニメーションを使用", style.animationEnabled); });
		if (!style.animationEnabled) {
			return;
		}
		drawField(anyItemActive, [&]() {
			return Engine::InspectorDrawerCommon::DrawCheckboxField("アニメーションクリップを使用", style.useAnimationClip);
		});
		if (style.useAnimationClip) {
			if (context.editorContext) {
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::AssetReferenceField("アニメーションクリップ", style.animationClip,
						context.editorContext->assetDatabase, {Engine::AssetType::AnimationClip});
				});
			}
		} else {
			ImGui::SeparatorText("色");
			{
				ImGui::PushID("UIColorAnim");
				drawField(anyItemActive, [&]() { return Engine::MyGUI::ColorEdit("色", style.color); });
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragFloat(
						"遷移時間", style.colorTransitionDuration, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f});
				});
				drawField(anyItemActive,
					[&]() { return Engine::InspectorDrawerCommon::DrawEnumComboField("イージング", style.colorEasing); });
				ImGui::PopID();
			}
			ImGui::SeparatorText("スケール");
			{
				ImGui::PushID("UIScaleAnim");
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragVector2(
						"スケール", style.scale, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 8.0f});
				});
				drawField(anyItemActive, [&]() {
					return Engine::MyGUI::DragFloat(
						"遷移時間", style.scaleTransitionDuration, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10.0f});
				});
				drawField(anyItemActive,
					[&]() { return Engine::InspectorDrawerCommon::DrawEnumComboField("イージング", style.scaleEasing); });
				ImGui::PopID();
			}
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionTextureFields(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		drawField(anyItemActive,
			[&]() { return Engine::InspectorDrawerCommon::DrawCheckboxField("上書きするか", style.overrideTexture); });
		if (style.overrideTexture && context.editorContext) {
			drawField(anyItemActive, [&]() {
				return Engine::MyGUI::AssetReferenceField(
					"テクスチャ", style.texture, context.editorContext->assetDatabase, {Engine::AssetType::Texture});
			});
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionSoundFields(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		if (context.editorContext) {
			drawField(anyItemActive, [&]() {
				return Engine::MyGUI::AssetReferenceField(
					"オーディオクリップ", style.sound, context.editorContext->assetDatabase, {Engine::AssetType::Audio});
			});
		}
		drawField(anyItemActive, [&]() {
			return Engine::MyGUI::DragFloat(
				"音量", style.soundVolume, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f});
		});
	}

	template <class DrawFieldFn>
	void DrawTransitionStyleFields(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		if (ImGui::BeginTabBar("##UITransitionStyleTabs")) {
			if (ImGui::BeginTabItem("アニメーション")) {
				DrawTransitionAnimationFields(drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("テクスチャ")) {
				DrawTransitionTextureFields(drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("サウンド")) {
				DrawTransitionSoundFields(drawField, context, style, anyItemActive);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	template <class DrawFieldFn>
	void DrawTransitionStyle(DrawFieldFn&& drawField, const Engine::EditorPanelContext& context, const char* label,
		Engine::UITransitionStyle& style, bool& anyItemActive) {

		ImGui::Indent();
		if (!Engine::MyGUI::CollapsingHeader(label, false)) {
			ImGui::Unindent();
			return;
		}
		ImGui::PushID(label);
		DrawTransitionStyleFields(drawField, context, style, anyItemActive);
		ImGui::PopID();
		ImGui::Unindent();
	}
}

Engine::UISelectableInspectorDrawer::UISelectableInspectorDrawer()
	: SerializedComponentInspectorDrawer("UI Selectable", "UISelectable") {
}

void Engine::UISelectableInspectorDrawer::DrawFields(const EditorPanelContext& context, [[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive,
		[&]() { return InspectorDrawerCommon::DrawCheckboxField("入力操作対象、アクティブ設定", draft.interactable); });

	auto drawField = [&](bool& active, auto&& function) { DrawField(active, std::forward<decltype(function)>(function)); };
	// 状態ごとの表示と音声を編集する
	DrawTransitionStyle(drawField, context, "通常", draft.normal, anyItemActive);
	DrawTransitionStyle(drawField, context, "選択", draft.selected, anyItemActive);
	DrawTransitionStyle(drawField, context, "決定", draft.submitted, anyItemActive);
	DrawTransitionStyle(drawField, context, "無効", draft.disabled, anyItemActive);
}

void Engine::UISelectableInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity, const UISelectableComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<UISelectableComponent>(entity)) {
		return;
	}
	// 実行状態を残して編集値だけを反映する
	ApplyUISelectableAuthoring(previewComponent, world.GetComponent<UISelectableComponent>(entity));
}
