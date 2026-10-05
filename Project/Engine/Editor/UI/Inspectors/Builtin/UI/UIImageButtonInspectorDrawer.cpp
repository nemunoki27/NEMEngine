#include "UIImageButtonInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>

//============================================================================
//	UIImageButtonInspectorDrawer classMethods
//============================================================================

Engine::UIImageButtonInspectorDrawer::UIImageButtonInspectorDrawer()
	: SerializedComponentInspectorDrawer("UI Image Button", "UIImageButton") {
}

void Engine::UIImageButtonInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::InputText("アクション名", draft.actionName); });
	if (!world.HasComponent<SpriteRendererComponent>(entity)) {
		ImGui::TextDisabled("Sprite Rendererが必要です");
	}
}
