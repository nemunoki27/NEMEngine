#include "DecalProjectorInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/DecalMaterialFields.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

void Engine::DecalProjectorInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::AssetReferenceField("マテリアル", draft.material,
		context.editorContext->assetDatabase, {AssetType::Material}); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "表示レイヤー", draft.renderingLayerMask); });
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "受け取り先レイヤー", draft.receiverRenderingLayerMask); });
	// 寸法とPivotは親Transformのローカル空間で指定する
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("大きさと奥行き", draft.size, {.minValue = 0.001f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("中心のオフセット", draft.pivot); });
	DrawField(anyItemActive, [&]() { return MyGUI::ColorEdit("乗算色", draft.color); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("不透明度", draft.fadeFactor, {.minValue = 0.0f, .maxValue = 1.0f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("UVスケール", draft.uvScale); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("UVオフセット", draft.uvBias); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("UV回転", draft.uvRotation); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("重なり順", draft.sortOrder); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("描画距離", draft.drawDistance, {.minValue = 0.0f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("距離フェード開始", draft.fadeScale, {.minValue = 0.0f, .maxValue = 1.0f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("角度フェード開始", draft.startAngleFade, {.minValue = 0.0f, .maxValue = 89.9f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("角度フェード終了", draft.endAngleFade, {.minValue = 0.01f, .maxValue = 90.0f}); });
	// 個別設定の見出しと項目を一段下げる
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("個別マテリアル設定")) {
		static const MaterialAsset fallback = CreateDefaultDecalMaterialAsset("DefaultDecal");
		const auto* material = draft.material && context.renderPipeline ?
			context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(draft.material) : &fallback;
		if (material && material->domain == MaterialDomain::Decal) {
			DrawField(anyItemActive, [&]() { return DecalMaterialFields::Draw(draft.materialInstance, material->parameters,
				context.editorContext->assetDatabase); });
		} else {
			ImGui::TextDisabled("デカール用マテリアルを設定してください");
		}
	}
	ImGui::Unindent();
}
