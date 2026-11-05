#include "EditorAntialiasingMenu.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>


//============================================================================
//	EditorAntialiasingMenu functions
//============================================================================

void Engine::EditorAntialiasingMenu::Draw(const EditorPanelContext& context) {

	ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
	if (!ImGui::BeginMenu("アンチエイリアス")) return;
	auto& controller = context.graphicsPlatform->GetFeatureController();
	const auto& preferences = controller.GetPreferences();
	AntialiasingMethod method = preferences.antialiasing;
	if (MyGUI::EnumCombo("方式", method).valueChanged) controller.SetAntialiasing(method);
	if (method == AntialiasingMethod::FXAA) {
		ImGui::Separator();
		auto settings = preferences.fxaa;
		const FloatEditSetting setting{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 1.0f};
		bool changed = MyGUI::DragFloat("強度", settings.strength, setting).valueChanged;
		changed |= MyGUI::DragFloat("輪郭の閾値", settings.edgeThreshold, setting).valueChanged;
		changed |= MyGUI::DragFloat("最小閾値", settings.minimumThreshold, setting).valueChanged;
		changed |= MyGUI::DragFloat("サブピクセル強度", settings.subpixelStrength, setting).valueChanged;
		if (changed) controller.SetFXAASettings(settings);
	}
	ImGui::EndMenu();
}
