#include "HexTilingInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <format>

//============================================================================
//	HexTilingInspectorDrawer classMethods
//============================================================================
void Engine::HexTilingInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("タイルサイズ", draft.tileSize, {.dragSpeed = 0.01f, .minValue = 0.001f, .maxValue = 100000.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("ランダム回転角", draft.randomRotation, {.dragSpeed = 0.5f, .minValue = 0.0f, .maxValue = 180.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::DragInt("シード", draft.seed); });

	const auto subMeshes = GetMeshSubMeshes(world, entity);
	if (subMeshes.empty() || !MyGUI::CollapsingHeader("適用するサブメッシュ")) return;
	for (uint32_t index = 0; index < subMeshes.size(); ++index) {
		const auto& subMesh = subMeshes[index];
		const auto label = subMesh.name.empty() ? std::format("サブメッシュ {}", index) : subMesh.name;
		ImGui::PushID(static_cast<int>(index));
		DrawField(anyItemActive, [&] {
			bool enabled = IsHexTilingSubMeshEnabled(draft, subMesh, index);
			const auto result = InspectorDrawerCommon::DrawCheckboxField(label.c_str(), enabled);
			if (result.valueChanged) SetHexTilingSubMeshEnabled(draft, subMesh, index, enabled);
			return result;
		});
		ImGui::PopID();
	}
}
