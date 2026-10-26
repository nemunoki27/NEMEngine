#include "ParticleFieldInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Particle/GUI/ParticleGUIHelpers.h>

using namespace Engine;

void ForceFieldInspectorDrawer::DrawFields(const EditorPanelContext& context, [[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& active) {

	auto& draft = GetDraft();
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(active, [&]() { return MyGUI::EnumCombo("種類", draft.type); });
	DrawField(active, [&]() { return MyGUI::DragVector3("軸方向", draft.axis); });
	DrawField(active, [&]() { return MyGUI::DragFloat("強さ", draft.strength); });
	DrawField(active, [&]() { return MyGUI::DragFloat("範囲", draft.range, ParticleGUI::MakeDragSetting(0.001f, 100000)); });
	DrawField(active, [&]() { return MyGUI::DragFloat("減衰", draft.falloff, ParticleGUI::MakeDragSetting(0, 16)); });
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "対象Layer", draft.layerMask); });
}

void VectorFieldInspectorDrawer::DrawFields(const EditorPanelContext& context, [[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& active) {

	auto& draft = GetDraft();
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(active, [&]() { return MyGUI::EnumCombo("種類", draft.preset); });
	if (draft.preset == ParticleVectorFieldPreset::Asset) DrawField(active, [&]() {
		return MyGUI::AssetReferenceField("FGA", draft.field, context.editorContext->assetDatabase, {AssetType::DefaultAsset});
	});
	DrawField(active, [&]() { return MyGUI::DragVector3("範囲", draft.size, ParticleGUI::MakeDragSetting(0.001f, 100000)); });
	DrawField(active, [&]() { return MyGUI::DragFloat("強さ", draft.intensity); });
	DrawField(active, [&]() { return MyGUI::DragFloat("追従", draft.tightness, ParticleGUI::MakeDragSetting(0, 1)); });
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "対象Layer", draft.layerMask); });
}

void HeightmapColliderInspectorDrawer::DrawFields(const EditorPanelContext& context, [[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity, bool& active) {

	auto& draft = GetDraft();
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(active, [&]() { return MyGUI::AssetReferenceField("高さ画像", draft.heightmap,
		context.editorContext->assetDatabase, {AssetType::Texture}); });
	DrawField(active, [&]() { return MyGUI::DragVector3("範囲と高さ", draft.size, ParticleGUI::MakeDragSetting(0.001f, 100000)); });
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawLayerMaskField(context, "対象Layer", draft.layerMask); });
}
