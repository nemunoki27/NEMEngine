#include "ProjectPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Utility/EditorShell.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	ProjectPanel classMethods
//============================================================================
void Engine::ProjectPanel::DrawSceneStoragePopup(const EditorPanelContext& context, AssetDatabase& database) {

	auto inspect = [&]() {
		sceneStorageIssues_ = context.editorContext->sceneStorage->Inspect(database);
		sceneRecoveries_ = context.editorContext->sceneStorage->GetRecoveries();
		sceneRecoveryLabels_.clear();
		for (const auto& recovery : sceneRecoveries_) {
			const auto journal = JsonAdapter::Load(recovery / "operation.json", false);
			if (!journal.is_object() || !journal.contains("createdAtUtc") || !journal["createdAtUtc"].is_string() ||
				!journal.contains("label") || !journal["label"].is_string() || !journal.contains("state") || !journal["state"].is_string()) {
				sceneRecoveryLabels_.push_back("操作記録の復旧が必要です / " + Algorithm::PathToUTF8(recovery.filename()));
				continue;
			}
			sceneRecoveryLabels_.push_back(journal.value("createdAtUtc", std::string{}) + " UTC / " + journal.value("label", std::string{}) + " / " + journal.value("state", std::string{}) +
				" / " + Algorithm::PathToUTF8(recovery.filename()));
		}
		sceneStorageRevision_ = database.GetStructureRevision();
		selectedSceneIssue_ = -1;
		confirmActorRemoval_ = false;
		sceneRemovalPreview_.clear();
	};
	if (sceneStorageRevision_ != database.GetStructureRevision()) {
		inspect();
		if (!context.editorContext->sceneStorage->GetRecoveries(true).empty()) {
			sceneStorageMessage_ = "完了していない保存・削除・修復があります、退避した操作を確認してください";
			requestSceneStoragePopup_ = true;
		}
	}
	if (ImGui::Button("シーンデータの検証・修復")) {
		inspect();
		requestSceneStoragePopup_ = true;
	}
	if (!sceneStorageIssues_.empty()) {
		ImGui::SameLine();
		ImGui::Text("問題: %zu件", sceneStorageIssues_.size());
	}
	if (requestSceneStoragePopup_) {
		ImGui::OpenPopup("シーンデータの検証・修復");
		requestSceneStoragePopup_ = false;
	}
	ImGui::SetNextWindowSize(ImVec2(850.0f, 600.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::BeginPopupModal("シーンデータの検証・修復", nullptr, ImGuiWindowFlags_None)) return;
	if (ImGui::Button("再検証")) inspect();
	ImGui::SameLine();
	if (ImGui::Button("退避フォルダーを開く")) EditorShell::OpenDirectory(RuntimePaths::GetSavedRoot() / "SceneAssetRecovery");
	ImGui::TextWrapped("欠損は自動削除しません。修復対象のシーンは先に閉じてください。退避した操作を戻すときは、その操作が変更したファイル全体が対象です。");
	if (!sceneStorageMessage_.empty()) ImGui::TextWrapped("%s", sceneStorageMessage_.c_str());
	ImGui::Separator();
	if (ImGui::BeginChild("SceneStorageIssues", ImVec2(0, 190), true)) {
		if (sceneStorageIssues_.empty()) ImGui::TextUnformatted("シーンとExternalActorの欠損はありません");
		for (size_t i = 0; i < sceneStorageIssues_.size(); ++i) {
			const auto& issue = sceneStorageIssues_[i];
			ImGui::PushID(static_cast<int>(i));
			std::string text = issue.detail + " / " + Algorithm::PathToUTF8(issue.scenePath.filename()) +
				" / " + Algorithm::PathToUTF8(issue.actorPath.filename());
			if (ImGui::Selectable(text.c_str(), selectedSceneIssue_ == static_cast<int>(i))) {
				selectedSceneIssue_ = static_cast<int>(i);
				confirmActorRemoval_ = false;
				sceneRemovalPreview_.clear();
				actorRestorePath_.clear();
			}
			ImGui::PopID();
		}
	}
	ImGui::EndChild();
	const bool editing = context.editorContext && !context.editorContext->isPlaying && !context.editorContext->isPrefabEditing;
	if (selectedSceneIssue_ >= 0 && static_cast<size_t>(selectedSceneIssue_) < sceneStorageIssues_.size()) {
		const auto issue = sceneStorageIssues_[selectedSceneIssue_];
		ImGui::TextWrapped("シーン: %s", Algorithm::PathToUTF8(issue.scenePath).c_str());
		ImGui::TextWrapped("Actor: %s", Algorithm::PathToUTF8(issue.actorPath).c_str());
		ImGui::BeginDisabled(!editing || !issue.missing || !issue.actorID);
		ImGui::InputText("復元元のActorファイル", &actorRestorePath_);
		if (ImGui::Button("指定ファイルから復元")) {
			if (context.editorContext->sceneStorage->RestoreActor(issue.scenePath, issue.actorID,
				Algorithm::PathFromUTF8(actorRestorePath_), sceneStorageMessage_)) {
				sceneStorageMessage_ = "Actorを復元しました";
				database.RebuildMeta();
				inspect();
			}
		}
		ImGui::TextWrapped("削除確定では子Actorをローカル座標のままルートへ移します。型を確認できるEntity参照は解除し、不明な参照が残る場合は中断します。元の親のTransformは失われているため、復元できる場合は復元を推奨します。");
		if (ImGui::Button("削除確定の変更対象を確認")) {
			confirmActorRemoval_ = false;
			if (context.editorContext->sceneStorage->PreviewMissingActorRemoval(issue.scenePath, issue.actorID, sceneRemovalPreview_, sceneStorageMessage_)) {
				sceneStorageMessage_ = "変更対象を確認しました、内容を確認してから削除を確定してください";
			}
		}
		if (!sceneRemovalPreview_.empty() && ImGui::TreeNode("変更対象ファイル")) {
			for (const auto& path : sceneRemovalPreview_) ImGui::TextWrapped("%s", Algorithm::PathToUTF8(path).c_str());
			ImGui::TreePop();
		}
		ImGui::BeginDisabled(sceneRemovalPreview_.empty());
		ImGui::Checkbox("このActorの削除と子のルート移動を確定する", &confirmActorRemoval_);
		ImGui::BeginDisabled(!confirmActorRemoval_);
		if (ImGui::Button("欠損Actorの削除を確定")) {
			if (context.editorContext->sceneStorage->RemoveMissingActor(issue.scenePath, issue.actorID, sceneStorageMessage_)) {
				sceneStorageMessage_ = "削除を確定しました、操作前のデータは退避フォルダーに残しています";
				database.RebuildMeta();
				inspect();
			}
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::EndDisabled();
	}
	if (ImGui::TreeNode("退避した操作を戻す")) {
		ImGui::BeginDisabled(!editing);
		for (size_t i = 0; i < sceneRecoveries_.size(); ++i) {
			if (ImGui::Button(sceneRecoveryLabels_[i].c_str())) {
				actorRestorePath_ = Algorithm::PathToUTF8(sceneRecoveries_[i]);
				ImGui::OpenPopup("操作の復旧確認");
			}
		}
		if (ImGui::BeginPopupModal("操作の復旧確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextWrapped("この操作が変更した全ファイルを操作前へ戻します。操作後の変更がある場合は中断します。");
			ImGui::TextWrapped("%s", actorRestorePath_.c_str());
			if (ImGui::Button("戻す")) {
				if (context.editorContext->sceneStorage->Recover(Algorithm::PathFromUTF8(actorRestorePath_), sceneStorageMessage_)) {
					sceneStorageMessage_ = "操作前のファイルへ復旧しました";
					database.RebuildMeta();
					inspect();
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("キャンセル")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::EndDisabled();
		ImGui::TreePop();
	}
	if (ImGui::Button("閉じる")) ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
