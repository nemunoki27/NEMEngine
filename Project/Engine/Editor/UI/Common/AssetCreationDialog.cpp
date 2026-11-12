#include "AssetCreationDialog.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

//============================================================================
//	AssetCreationDialog methods
//============================================================================
void Engine::AssetCreationDialog::Open() {

	openRequested_ = true;
	diagnostic_.clear();
}

void Engine::AssetCreationDialog::Draw(const char* title, ProjectAssetFileKind kind,
	const std::function<bool(const std::string&, std::string&)>& create) {

	if (openRequested_) {
		if (name_.empty()) name_ = ProjectAssetFileUtility::GetDefaultName(kind);
		ImGui::OpenPopup(title);
		openRequested_ = false;
	}
	float buttonWidth = 240.0f;
	const auto& style = ImGui::GetStyle();
	const float popupWidth = buttonWidth * 2.0f + style.ItemSpacing.x + style.WindowPadding.x * 2.0f;
	if (!MyGUI::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize, popupWidth)) return;
	const auto before = ProjectAssetFileUtility::PlanToolAsset(kind, name_);
	ImGui::TextWrapped("保存先: %s", before.assetPath.empty() ? "GameAssets/" : before.assetPath.c_str());
	const auto edit = MyGUI::InputText("名前", name_, { .propertyRow = {.labelWidth = ImGui::CalcTextSize("名前 ").x, .rowWidth = 480.0f} });
	if (edit.valueChanged) diagnostic_.clear();
	const auto plan = ProjectAssetFileUtility::PlanToolAsset(kind, name_);
	ImGui::BeginDisabled(!plan.success);
	if (ImGui::Button("作成", ImVec2(buttonWidth, 0.0f))) {
		if (create(plan.assetPath, diagnostic_)) ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(buttonWidth, 0.0f))) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
