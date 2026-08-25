#include "TagManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Settings/ProjectTagSettings.h>
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>
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

	if (!ImGui::Begin("タグ・描画レイヤー", &openWindow_)) {
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
		| ImGuiTableFlags_SizingStretchProp;
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

	ImGui::Separator();
	if (ImGui::CollapsingHeader("Rendering Layer")) {
		if (ImGui::Button("Layer設定を保存")) {
			if (ProjectRenderingLayerSettings::Save()) {
				renderingLayersDirty_ = false;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Layer設定を再読込")) {
			ProjectRenderingLayerSettings::Reload();
			renderingLayersDirty_ = false;
		}
		if (renderingLayersDirty_) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f),
				"未保存の変更があります");
		}

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##AddRenderingLayer",
			"新しいLayer名", addRenderingLayerBuffer_,
			sizeof(addRenderingLayerBuffer_));
		ImGui::SameLine();
		const bool canAddLayer =
			ProjectRenderingLayerSettings::IsValidNewLayer(
				addRenderingLayerBuffer_);
		ImGui::BeginDisabled(!canAddLayer);
		if (ImGui::Button("追加##RenderingLayer")) {
			if (ProjectRenderingLayerSettings::AddLayer(
				addRenderingLayerBuffer_)) {

				addRenderingLayerBuffer_[0] = '\0';
				renderingLayersDirty_ = true;
			}
		}
		ImGui::EndDisabled();

		const auto& layerNames = ProjectRenderingLayerSettings::GetNames();
		for (uint32_t index = 0u;
			index < ProjectRenderingLayerSettings::kLayerCount; ++index) {

			if (layerNames[index].empty()) {
				continue;
			}
			char nameBuffer[128]{};
			std::snprintf(nameBuffer, sizeof(nameBuffer), "%s",
				layerNames[index].c_str());
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("%u", index);
			ImGui::SameLine();
			const float deleteButtonWidth = 56.0f;
			ImGui::SetNextItemWidth(index == 0u ? -FLT_MIN :
				ImGui::GetContentRegionAvail().x - deleteButtonWidth);
			ImGui::BeginDisabled(index == 0u);
			if (ImGui::InputText("##LayerName", nameBuffer,
				sizeof(nameBuffer)) &&
				ProjectRenderingLayerSettings::SetName(index, nameBuffer)) {

				renderingLayersDirty_ = true;
			}
			ImGui::EndDisabled();
			if (index != 0u) {
				ImGui::SameLine();
				if (ImGui::Button("削除", ImVec2(deleteButtonWidth, 0.0f)) &&
					ProjectRenderingLayerSettings::RemoveLayer(index)) {

					renderingLayersDirty_ = true;
				}
			}
			ImGui::PopID();
		}
	}

	ImGui::End();
}
