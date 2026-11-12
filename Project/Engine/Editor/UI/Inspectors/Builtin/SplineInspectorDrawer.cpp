#include "SplineInspectorDrawer.h"
#include <Engine/Editor/UI/Common/LogicPlacementFields.h>
#include <Engine/Editor/Utility/LogicPlacementSpline.h>
#include <Engine/Editor/Utility/LogicPlacementPreview.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

#include <array>

void Engine::SplineInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
	[[maybe_unused]] const SplineComponent& component) {

	const auto* data = TryGetSpline(world, entity);
	data_ = data ? *data : SplineData{};
	placementPreview_.reset();
}

void Engine::SplineInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& active) {

	DrawField(active, [&]() {
		static const std::array<std::string, 2> names{"2D", "3D"};
		std::string dimension = data_.is2D ? "2D" : "3D";
		const auto result = MyGUI::StringCombo("次元", dimension, names);
		if (result.valueChanged) {
			data_.is2D = dimension == "2D";
			if (data_.is2D) for (auto& point : data_.points) point.position.z = point.tangentIn.z = point.tangentOut.z = 0;
		}
		return result;
	});
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("デバッグ描画", data_.drawDebug); });
	const auto drawVector = [&](const char* label, Vector3& value) {
		if (!data_.is2D) return MyGUI::DragVector3(label, value);
		Vector2 xy(value.x, value.y);
		auto result = MyGUI::DragVector2(label, xy);
		if (result.valueChanged) value = Vector3(xy.x, xy.y, 0);
		return result;
	};
	DrawField(active, [&]() { return InspectorDrawerCommon::DrawCheckboxField("閉じた曲線", data_.closed); });
	placementOpen_ = MyGUI::CollapsingHeader("制御点のロジック配置");
	if (placementOpen_) {
		ImGui::Indent();
		bool changed = placement_.is2D != data_.is2D;
		placement_.is2D = data_.is2D;
		placement_.fill = false;
		changed |= LogicPlacementFields::DrawShape(placement_, true);
		if (changed || !placementPreview_)
			placementPreview_ = std::make_shared<LogicPlacementResult>(BuildLogicPlacement(placement_, 4096));
		if (!placementPreview_->diagnostic.empty()) MyGUI::Text("配置", placementPreview_->diagnostic);
		ImGui::BeginDisabled(!placementPreview_->IsValid());
		if (ImGui::Button("制御点を生成", ImVec2(ImGui::GetContentRegionAvail().x, 0)) &&
			ApplyLogicPlacementSpline(placement_, *placementPreview_, data_)) {
			PushEditResult(ValueEditResult{true, false, true}, active);
		}
		ImGui::EndDisabled();
		if (data_.drawDebug) if (const auto* transform = world.TryGetComponent<TransformComponent>(entity))
			SubmitLogicPlacementPreview(context, placementPreview_, transform->worldMatrix, data_.is2D, world.GetUUID(entity));
		ImGui::Unindent();
	}
	for (size_t i = 0; i < data_.points.size(); ++i) {
		ImGui::PushID(static_cast<int32_t>(i));
		if (ImGui::TreeNode("Point", "制御点 %zu", i)) {
			auto& point = data_.points[i];
			DrawField(active, [&]() { return drawVector("位置", point.position); });
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
