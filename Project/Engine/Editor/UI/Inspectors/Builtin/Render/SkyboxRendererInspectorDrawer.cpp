#include "SkyboxRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	SkyboxRendererInspectorDrawer classMethods
//============================================================================
void Engine::SkyboxRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

	DrawField(anyItemActive, [&]() {
		return MyGUI::AssetReferenceField("cubemap", draft.cubemapTexture,
			context.editorContext->assetDatabase, { AssetType::Texture });
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("色", draft.color);
		});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("IBL強度", draft.iblIntensity, { .dragSpeed = 0.01f, .minValue = 0.0f });
		});
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});
}
