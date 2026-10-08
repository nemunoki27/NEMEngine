#include "AnimationControllerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

void Engine::AnimationControllerTool::DrawPreview(const EditorToolContext& context) {

	if (!ImGui::CollapsingHeader("Sceneプレビュー")) {

		preview_.End();
		return;
	}
	ECSWorld* world = context.GetWorld();
	SystemContext* systemContext = context.toolContext.systemContext;
	if (!world || !systemContext || !context.panelContext || !context.panelContext->editorState) return;
	if (!preview_.IsActive()) {

		const auto& selected = context.panelContext->editorState->GetSelectedEntities();
		ImGui::BeginDisabled(selected.size() != 1 || context.IsPlaying() || !context.CanEditScene());
		if (ImGui::Button("選択Entityで再生") && preview_.Begin(*world, selected.front(), session_.GetDraft())) {

			previewRevision_ = session_.GetRevision();
		}
		ImGui::EndDisabled();
	} else if (ImGui::Button("停止して復元")) preview_.End();
	if (!preview_.IsActive()) return;
	// 編集用の初期値とは別に実行Parameterを操作する
	ImGui::PushID("ControllerPreview");
	for (const auto& parameter : session_.GetDraft().parameters) {

		const auto* current = AnimationControllerEvaluator::GetParameter(preview_.GetRuntime(), parameter.name);
		if (!current) continue;
		AnimationControllerParameterValue value = *current;
		bool changed = false;
		if (auto* floatValue = std::get_if<float>(&value)) changed = ImGui::DragFloat(parameter.name.c_str(), floatValue, 0.01f);
		else if (auto* integerValue = std::get_if<int32_t>(&value)) changed = ImGui::InputInt(parameter.name.c_str(), integerValue);
		else if (parameter.type == AnimationControllerParameterType::Trigger) {

			if (ImGui::Button(parameter.name.c_str())) {

				value = true;
				changed = true;
			}
		} else changed = ImGui::Checkbox(parameter.name.c_str(), &std::get<bool>(value));
		if (changed) preview_.SetParameter(parameter.name, value);
	}
	ImGui::PopID();
	if (!context.IsPlaying() && context.CanEditScene()) preview_.Update(*world, *systemContext);
	ImGui::Text("状態: %s", preview_.GetState().c_str());
}
