#include "LightInfluenceFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>

Engine::ValueEditResult Engine::DrawLightInfluenceFields(const EditorPanelContext& context,
	ECSWorld& world, Entity owner, LightInfluenceSettings& settings) {

	ValueEditResult result;
	result.Merge(InspectorDrawerCommon::DrawLayerMaskField(context, "照射対象レイヤー", settings.renderingLayerMask));
	ImGui::Separator();
	result.Merge(InspectorDrawerCommon::DrawLayerMaskField(context, "影の除外レイヤー", settings.shadowExcludedLayerMask));
	ImGui::Separator();
	MyGUI::Text("影の除外オブジェクト");
	ImGui::Indent();
	for (size_t index = 0; index < settings.shadowExclusions.size();) {

		ImGui::PushID(static_cast<int>(index));
		auto& exclusion = settings.shadowExclusions[index];
		result.Merge(SceneEntityReferenceFields::Draw("除外対象", world, owner, exclusion.target, {}, &exclusion.runtimeTargetID));
		result.Merge(MyGUI::CheckboxField("子も含める", exclusion.includeChildren));
		const bool removed = ImGui::Button("削除");
		if (removed) {
			settings.shadowExclusions.erase(settings.shadowExclusions.begin() + index);
			result.valueChanged = result.editFinished = true;
		} else ++index;
		ImGui::Separator();
		ImGui::PopID();
	}
	if (ImGui::Button("追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		settings.shadowExclusions.emplace_back();
		result.valueChanged = result.editFinished = true;
	}
	ImGui::Unindent();
	return result;
}
