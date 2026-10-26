#include "SplineInspectorDrawer.h"

void Engine::SplineInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const SplineComponent& component) {

	const auto* data = TryGetSpline(world, entity);
	data_ = data ? *data : SplineData{};
}

void Engine::SplineInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& active) {

	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("閉じた曲線", data_.closed); });
	for (size_t i = 0; i < data_.points.size(); ++i) {
		ImGui::PushID(static_cast<int32_t>(i));
		if (ImGui::TreeNode("Point", "制御点 %zu", i)) {
			auto& point = data_.points[i];
			DrawField(active, [&]() { return MyGUI::DragVector3("位置", point.position); });
			DrawField(active, [&]() { return MyGUI::DragVector3("入る接線", point.tangentIn); });
			DrawField(active, [&]() { return MyGUI::DragVector3("出る接線", point.tangentOut); });
			if (ImGui::Button("削除")) {
				data_.points.erase(data_.points.begin() + i);
				PushEditResult(ValueEditResult{true, false, true}, active);
				ImGui::TreePop();
				ImGui::PopID();
				break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (data_.points.size() < 4096 && ImGui::Button("制御点を追加")) {
		SplineControlPoint point;
		if (!data_.points.empty()) point.position = data_.points.back().position + Vector3(1, 0, 0);
		data_.points.push_back(point);
		PushEditResult(ValueEditResult{true, false, true}, active);
	}
}

void Engine::SplineInspectorDrawer::SerializeDraft([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const SplineComponent& component, nlohmann::json& out) const {

	to_json(out, data_);
}

void Engine::SplineInspectorDrawer::ApplyPreview(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const SplineComponent& component) {

	SetSpline(world, entity, data_);
}
