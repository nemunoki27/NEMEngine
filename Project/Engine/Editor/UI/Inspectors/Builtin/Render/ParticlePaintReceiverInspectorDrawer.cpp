#include "ParticlePaintReceiverInspectorDrawer.h"

void Engine::ParticlePaintReceiverInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::EnumCombo("投影方式", draft.projection); });
	int32_t resolution = static_cast<int32_t>(draft.resolution);
	DrawField(anyItemActive, [&]() {
		auto result = MyGUI::DragInt("Texture解像度", resolution, {.minValue = 32, .maxValue = 2048});
		if (result.valueChanged) draft.resolution = static_cast<uint32_t>(resolution);
		return result;
	});
	DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("投影範囲", draft.planarSize, {.minValue = 0.001f}); });
	DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("塗装の不透明度", draft.opacity, {.minValue = 0.0f, .maxValue = 1.0f}); });
	if (ImGui::Button("塗装を消去")) ClearParticlePaint(world, entity);
	ImGui::TextDisabled("塗装はPlay終了とScene解放で消去されます");
}
