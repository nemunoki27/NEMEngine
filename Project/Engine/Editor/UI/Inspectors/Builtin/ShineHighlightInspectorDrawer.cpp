#include "ShineHighlightInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	ShineHighlightInspectorDrawer classMethods
//============================================================================
void Engine::ShineHighlightInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("位置", draft.position, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }); });
	DrawField(anyItemActive, [&] { return MyGUI::ColorEdit("色", draft.color); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("強度", draft.intensity, { .dragSpeed = 0.01f, .minValue = 0.0f }); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("角度", draft.angle, { .dragSpeed = 0.5f }); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("幅", draft.width, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("ぼかし", draft.softness, { .dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f }); });
	DrawField(anyItemActive, [&] {
		return MyGUI::AssetReferenceField("ランプテクスチャ", draft.rampTexture,
			context.editorContext->assetDatabase, { AssetType::Texture });
	});
}
