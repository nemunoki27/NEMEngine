#include "FillMeshRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>

//============================================================================
//	FillMeshRendererInspectorDrawer classMethods
//============================================================================
void Engine::FillMeshRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// マテリアルと色
	DrawField(anyItemActive, [&]() {
		AssetEditSetting setting{};
		setting.defaultAssetID = DefaultMaterialSettings::GetInstance().GetFillMeshOrBuiltin();
		return MyGUI::AssetReferenceField("マテリアル", draft.material,
			context.editorContext->assetDatabase, { AssetType::Material }, setting);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});

	// 面を構成するXZ平面の点列
	for (size_t i = 0; i < draft.facePositions.size(); ++i) {

		ImGui::PushID(static_cast<int>(i));
		DrawField(anyItemActive, [&]() {
			return MyGUI::DragVector3("点", draft.facePositions[i], { .dragSpeed = 0.1f });
			});
		ImGui::PopID();
	}
	DrawField(anyItemActive, [&]() {

		ValueEditResult result{};
		if (ImGui::Button("点を追加")) {
			draft.facePositions.push_back(Vector3::AnyInit(0.0f));
			result.valueChanged = true;
			result.editFinished = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("末尾を削除") && !draft.facePositions.empty()) {
			draft.facePositions.pop_back();
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
		});

	// 押すと次フレームにメッシュを構築する
	DrawField(anyItemActive, [&]() {

		ValueEditResult result{};
		if (ImGui::Button("メッシュ構築")) {
			draft.buildMesh = true;
			result.valueChanged = true;
			result.editFinished = true;
		}
		return result;
		});

	// 描画パラメータ
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
}
