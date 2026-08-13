#include "TagManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Commands/Entity/RemapEntityTagsCommand.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <cstring>
#include <memory>
#include <string>
#include <vector>

void Engine::TagManagerTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::TagManagerTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::TagManagerTool::RequestRemap(const EditorToolContext& context, const std::string& from, const std::string& to) {

	// 開いているシーンのタグ文字列だけ付け替える、コマンド経由でUndoできる
	if (!context.CanEditScene() || !context.panelContext || !context.panelContext->host) {
		return;
	}
	context.panelContext->host->ExecuteEditorCommand(std::make_unique<RemapEntityTagsCommand>(from, to));
}

void Engine::TagManagerTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Tag Manager", &openWindow_)) {
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("ゲームプレイ用タグの一覧");
	ImGui::Separator();

	// 保存は明示操作で行い、編集済み状態はdirtyで知らせる
	if (ImGui::Button("保存")) {
		if (ProjectTagSettings::Save()) {
			dirty_ = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("再読込")) {
		ProjectTagSettings::Reload();
		dirty_ = false;
	}
	if (dirty_) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "未保存の変更があります");
	}

	// 新規タグ追加、入力が有効なときだけ追加できる
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputTextWithHint("##AddTag", "新しいタグ名", addBuffer_, sizeof(addBuffer_));
	ImGui::SameLine();
	const bool canAdd = ProjectTagSettings::IsValidNewTag(addBuffer_);
	ImGui::BeginDisabled(!canAdd);
	if (ImGui::Button("追加")) {
		if (ProjectTagSettings::AddTag(addBuffer_)) {
			addBuffer_[0] = '\0';
			dirty_ = true;
		}
	}
	ImGui::EndDisabled();

	ImGui::Separator();

	// 編集中にg_tagsが変化してもよいように一覧は複製して走査する
	const std::vector<std::string> tags = ProjectTagSettings::GetTags();
	const bool canEditScene = context.CanEditScene();

	const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		| ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##TagTable", 2, tableFlags)) {

		ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 180.0f);
		ImGui::TableHeadersRow();

		for (int32_t i = 0; i < static_cast<int32_t>(tags.size()); ++i) {

			const std::string& tag = tags[i];
			// Untaggedは予約タグなので削除リネーム不可
			const bool reserved = tag == "Untagged";

			ImGui::TableNextRow();
			ImGui::PushID(i);

			ImGui::TableSetColumnIndex(0);
			if (renamingTag_ == tag) {

				// リネーム入力中、確定で付け替えコマンドを発行する
				ImGui::SetNextItemWidth(-FLT_MIN);
				const bool committed = ImGui::InputText("##RenameTag", renameBuffer_, sizeof(renameBuffer_),
					ImGuiInputTextFlags_EnterReturnsTrue);
				if (committed) {
					if (ProjectTagSettings::RenameTag(tag, renameBuffer_)) {
						RequestRemap(context, tag, renameBuffer_);
						dirty_ = true;
					}
					renamingTag_.clear();
				}
			} else {
				ImGui::TextUnformatted(tag.c_str());
			}

			ImGui::TableSetColumnIndex(1);
			if (renamingTag_ == tag) {

				if (ImGui::SmallButton("確定")) {
					if (ProjectTagSettings::RenameTag(tag, renameBuffer_)) {
						RequestRemap(context, tag, renameBuffer_);
						dirty_ = true;
					}
					renamingTag_.clear();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("取消")) {
					renamingTag_.clear();
				}
			} else {

				// 予約タグや編集不可状態では操作させない
				ImGui::BeginDisabled(reserved || !canEditScene);
				if (ImGui::SmallButton("リネーム")) {
					renamingTag_ = tag;
					std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", tag.c_str());
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("削除")) {
					if (ProjectTagSettings::RemoveTag(tag)) {
						// 使用中のエンティティはUntaggedへ戻す
						RequestRemap(context, tag, "Untagged");
						dirty_ = true;
					}
				}
				ImGui::EndDisabled();
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	if (!canEditScene) {
		ImGui::TextDisabled("削除リネームはシーン編集中のみ行えます。");
	}

	ImGui::End();
}
