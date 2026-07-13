#include "ParticleEmitterInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	ParticleEmitterInspectorDrawer classMethods
//============================================================================
void Engine::ParticleEmitterInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	// エフェクトアセット
	{
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
			setting.defaultAssetID = BuiltinAssets::Effects::DefaultParticle;
			return MyGUI::AssetReferenceField("エフェクト", draft.effect,
				context.editorContext->assetDatabase, { AssetType::ParticleEffect }, setting);
			});
	}
	// 再生設定
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("再生", draft.playing);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("編集中も再生", draft.playInEditMode);
			});
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawCheckboxField("エミッター形状を描画", draft.drawEmitterShape);
			});
	}
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("レイヤー", draft.layer); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragInt("描画順", draft.order); });
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});
}
