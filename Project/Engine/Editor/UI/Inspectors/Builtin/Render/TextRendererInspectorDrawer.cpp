#include "TextRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	TextRendererInspectorDrawer classMethods
//============================================================================
void Engine::TextRendererInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	// ドラフトコンポーネントを参照
	auto& draft = GetDraft();

	//============================================================================
	//	アセットファイル
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::AssetReferenceField("フォント", draft.font,
				context.editorContext->assetDatabase, { AssetType::Font });
			});
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
			setting.defaultAssetID = BuiltinAssets::Materials::DefaultText;
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	//============================================================================
	//	テキスト見た目パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::InputText("テキスト", draft.text, { .multiLine = true });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("フォントサイズ", draft.fontSize,
				{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 10000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragFloat("文字間隔", draft.charSpacing,
				{ .dragSpeed = 0.01f, .minValue = -1000.0f, .maxValue = 1000.0f });
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::ColorEdit("色", draft.color);
			});

		// アウトライン
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("アウトライン", draft.enableOutline);
			});
		if (draft.enableOutline) {

			DrawField(anyItemActive, [&]() {
				return MyGUI::ColorEdit("アウトライン色", draft.outlineColor);
				});
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("アウトライン幅", draft.outlineWidth,
					{ .dragSpeed = 0.1f, .minValue = 0.0f, .maxValue = 100.0f });
				});
		}
	}
	//============================================================================
	//	テキスト描画パラメータ
	//============================================================================
	{
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("レイヤー", draft.layer);
			});
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragInt("描画順", draft.order);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("ブレンドモード", draft.blendMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("キュー", draft.queue);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("次元", draft.dimension);
			});
		// ワールドスケールは3D描画でのみ意味を持つので2Dでは表示しない
		if (draft.dimension == Dimension::Type3D) {
			DrawField(anyItemActive, [&]() {
				return MyGUI::DragFloat("ワールドスケール", draft.worldScale,
					{ .dragSpeed = 0.001f, .minValue = 0.0001f, .maxValue = 1000.0f });
				});
		}
	}
	//============================================================================
	//	文字ごとのトランスフォーム
	//============================================================================
	{
		ImGui::SeparatorText("文字ごとのトランスフォーム");

		// 表示中のグリフ数に合わせて行を用意する、値を編集したときだけコミットされる
		// 非表示などでレイアウト未生成のときはサイズ0で潰さず保存済みデータを温存する
		const size_t glyphCount = draft.runtimeLayout.glyphs.size();
		if (glyphCount == 0) {

			ImGui::TextDisabled("表示中の文字がありません");
		} else {

			if (draft.charTransforms.size() != glyphCount) {
				draft.charTransforms.resize(glyphCount);
			}
			for (size_t i = 0; i < draft.charTransforms.size(); ++i) {

				TextCharTransform& charTransform = draft.charTransforms[i];
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::TreeNode("CharTransform", "文字 %zu", i)) {

					DrawField(anyItemActive, [&]() {
						return MyGUI::DragVector2("位置", charTransform.translation,
							{ .dragSpeed = 0.1f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					DrawField(anyItemActive, [&]() {
						return MyGUI::DragFloat("回転", charTransform.rotation,
							{ .dragSpeed = 0.5f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					DrawField(anyItemActive, [&]() {
						return MyGUI::DragVector2("スケール", charTransform.scale,
							{ .dragSpeed = 0.01f, .minValue = -100000.0f, .maxValue = 100000.0f });
						});
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
	}
}
