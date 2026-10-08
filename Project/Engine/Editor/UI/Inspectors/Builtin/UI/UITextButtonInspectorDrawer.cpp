#include "UITextButtonInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

//============================================================================
//	UITextButtonInspectorDrawer classMethods
//============================================================================

Engine::UITextButtonInspectorDrawer::UITextButtonInspectorDrawer()
	: SerializedComponentInspectorDrawer("UI Text Button", "UITextButton") {
}

void Engine::UITextButtonInspectorDrawer::DrawFields(
	[[maybe_unused]] const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("有効", draft.enabled); });
	DrawField(anyItemActive, [&]() { return MyGUI::InputText("アクション名", draft.actionName); });
	if (!world.HasComponent<TextRendererComponent>(entity)) {
		ImGui::TextDisabled("Text Rendererが必要です");
	}
}
