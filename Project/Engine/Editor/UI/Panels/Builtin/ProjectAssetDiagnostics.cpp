#include "ProjectAssetDiagnostics.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// imgui
#include <imgui.h>

void Engine::ProjectAssetDiagnostics::Draw(AssetDatabase& database, bool editing) {

	if (ImGui::Button("Assetの診断・修復")) {
		selected_.reset();
		confirmDelete_ = false;
		ImGui::OpenPopup("Assetの診断・修復");
	}
	if (!database.GetLastRebuildError().empty()) {
		ImGui::SameLine();
		ImGui::TextUnformatted("索引更新に失敗しました");
	}
	ImGui::SetNextWindowSize(ImVec2(820, 540), ImGuiCond_FirstUseEver);
	if (!ImGui::BeginPopupModal("Assetの診断・修復", nullptr, ImGuiWindowFlags_None)) {
		return;
	}
	ImGui::BeginDisabled(!editing);
	if (ImGui::Button("再検査")) {
		message_ = database.RebuildMeta() ? "再検査しました" : "再検査に失敗しました 旧索引を保持しています";
		selected_.reset();
		confirmDelete_ = false;
	}
	ImGui::EndDisabled();
	ImGui::TextWrapped("%s", database.GetLastRebuildError().c_str());
	ImGui::TextWrapped("%s", message_.c_str());
	if (ImGui::BeginChild("AssetIssues", ImVec2(0, 260), true)) {
		int index = 0;
		for (const auto& issue : database.GetIssues()) {
			ImGui::PushID(index++);
			if (ImGui::Selectable(issue.assetPath.c_str())) {
				selected_ = issue;
				confirmDelete_ = false;
			}
			ImGui::TextWrapped("%s", issue.detail.c_str());
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	ImGui::BeginDisabled(!editing);
	if (selected_) {
		ImGui::TextWrapped("対象: %s", selected_->assetPath.c_str());
		ImGui::TextWrapped("関連: %s", selected_->relatedPath.c_str());
		if (selected_->type == AssetDatabaseIssueType::OrphanMeta) {
			ImGui::Checkbox("このmetaを削除する", &confirmDelete_);
			ImGui::BeginDisabled(!confirmDelete_);
			if (ImGui::Button("選択したmetaを削除")) {
				const bool removed = database.DeleteOrphanMeta(Algorithm::PathFromUTF8(selected_->relatedPath));
				message_ = removed ? "metaを削除しました" : "削除できません 元Assetの復活やファイル状態を再検査してください";
				selected_.reset();
				confirmDelete_ = false;
			}
			ImGui::EndDisabled();
		} else if (selected_->type == AssetDatabaseIssueType::FontAtlasRepair && selected_->referencedAssetID) {
			if (ImGui::Button("候補のAtlasへ接続")) {
				try {
					const bool repaired = database.RepairFontAtlas(selected_->assetID, selected_->referencedAssetID, &message_);
					if (message_.empty()) {
						message_ = repaired ? "Atlas参照を修復しました" : "修復できません 対象を再検査してください";
					}
				} catch (const std::exception& error) {
					message_ = error.what();
				}
				selected_.reset();
			}
		}
	}
	ImGui::EndDisabled();
	if (ImGui::Button("閉じる")) {
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
