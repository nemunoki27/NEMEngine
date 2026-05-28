#include "BillboardInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	BillboardInspectorDrawer classMethods
//============================================================================

namespace {

	Engine::ValueEditResult DrawAxisCheckbox(const char* label, Engine::BillboardComponent& billboard, Engine::Axis axis) {

		bool enabled = Engine::HasBillboardAxis(billboard, axis);
		Engine::ValueEditResult result = Engine::InspectorDrawerCommon::DrawCheckboxField(label, enabled);
		if (result.valueChanged) {
			Engine::SetBillboardAxis(billboard, axis, enabled);
		}
		return result;
	}

	Engine::ValueEditResult DrawAllAxesButton(Engine::BillboardComponent& billboard) {

		Engine::ValueEditResult result{};
		if (ImGui::Button("全軸", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
			Engine::SetBillboardAllAxes(billboard);
			result.valueChanged = true;
			result.editFinished = true;
		}
		result.anyItemActive = ImGui::IsItemActive();
		return result;
	}
}

void Engine::BillboardInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	SanitizeBillboardAxes(draft);

	DrawField(anyItemActive, [&]() {
		return DrawAllAxesButton(draft);
		});
	DrawField(anyItemActive, [&]() {
		return DrawAxisCheckbox("X", draft, Axis::X);
		});
	DrawField(anyItemActive, [&]() {
		return DrawAxisCheckbox("Y", draft, Axis::Y);
		});
	DrawField(anyItemActive, [&]() {
		return DrawAxisCheckbox("Z", draft, Axis::Z);
		});
}
