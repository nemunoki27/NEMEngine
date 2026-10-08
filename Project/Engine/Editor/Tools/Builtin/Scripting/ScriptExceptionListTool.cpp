#include "ScriptExceptionListTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Scripting/ManagedIDELauncher.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <string>

#include <imgui.h>

namespace {

	// 型名から表示用の短い名前を取得
	std::string ShortTypeName(const std::string& fullName) {
		const size_t dot = fullName.find_last_of('.');
		return dot == std::string::npos ? fullName : fullName.substr(dot + 1);
	}
}

void Engine::ScriptExceptionListTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::ScriptExceptionListTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::ScriptExceptionListTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Script Exception List", &openWindow_)) {
		ImGui::End();
		return;
	}

	ManagedScriptExceptionStore& store = ManagedScriptExceptionStore::GetInstance();

	ImGui::TextWrapped("Scriptの例外を表示します。Error Pauseが有効なら実行状態を保持して一時停止します。");
	ImGui::TextDisabled("identity は scriptTypeID の Stable GUID。表示名は識別には使いません。");

	// 表示を絞り込み、履歴を削除する
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputTextWithHint("##excFilter", "type/message で検索", textFilter_, sizeof(textFilter_));
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		store.Clear();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("count: %llu (bounded %llu)", static_cast<unsigned long long>(store.Count()),
		static_cast<unsigned long long>(ManagedScriptExceptionStore::kMaxEntries));
	ImGui::Separator();

	// 現在のWorldを選択先として確認
	ECSWorld* world = context.GetWorld();
	EditorState* editorState = context.panelContext ? context.panelContext->editorState : nullptr;

	const ImGuiTableFlags tableFlags =
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if (ImGui::BeginTable("##excTable", 5, tableFlags)) {

		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 70.0f);
		ImGui::TableSetupColumn("Callback", ImGuiTableColumnFlags_WidthFixed, 90.0f);
		ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthFixed, 120.0f);
		ImGui::TableSetupColumn("Exception", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (const ManagedScriptException& exc : store.Entries()) {

			const std::string shortType = ShortTypeName(exc.typeName);
			if (!Algorithm::ContainsCaseInsensitive(shortType, textFilter_) &&
				!Algorithm::ContainsCaseInsensitive(exc.message, textFilter_) &&
				!Algorithm::ContainsCaseInsensitive(exc.exceptionType, textFilter_)) {
				continue;
			}

			ImGui::TableNextRow();
			ImGui::PushID(std::to_string(exc.exceptionID).c_str());

			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(exc.timestamp.c_str());

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(exc.callback.c_str());

			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(shortType.c_str());
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s\nscriptTypeID: %s\nslot: %llu", exc.typeName.c_str(),
					exc.scriptTypeID.empty() ? "(unresolved)" : exc.scriptTypeID.c_str(),
					static_cast<unsigned long long>(exc.scriptSlotID));
			}

			ImGui::TableSetColumnIndex(3);
			// 報告元Worldに生存する所有Entityだけ選択
			const Entity owner = world ? exc.ResolveOwner(*world) : Entity::Null();
			const bool ownerAlive = owner.IsValid();
			const std::string entityLabel =
				exc.entityIndex == UINT32_MAX ? "所有Entityなし"
				: exc.entityName.empty() ? ("#" + std::to_string(exc.entityIndex) + ":" + std::to_string(exc.entityGeneration))
										 : exc.entityName;
			ImGui::BeginDisabled(!ownerAlive || !editorState);
			if (ImGui::SmallButton(entityLabel.c_str())) {
				if (editorState && ownerAlive) {
					editorState->SelectEntity(owner);
				}
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s",
					ownerAlive ? "クリックで該当 entity を選択" : "所有Entityがないか、報告元Worldは現在のWorldと異なります");
			}

			ImGui::TableSetColumnIndex(4);
			// 例外と呼出し位置を展開して表示
			const std::string header = exc.exceptionType + ": " + exc.message;
			if (ImGui::TreeNodeEx("##exc", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", header.c_str())) {

				if (exc.frames.empty()) {
					ImGui::TextDisabled("stack frame 情報はありません。");
				}
				for (size_t f = 0; f < exc.frames.size(); ++f) {

					const ManagedScriptExceptionFrame& frame = exc.frames[f];
					ImGui::PushID(static_cast<int>(f));
					// ソースパスを取得できた位置だけIDEで開く
					const bool jumpable = !frame.file.empty();
					std::string frameLabel = frame.method;
					if (jumpable) {
						frameLabel += "  " + frame.file + ":" + std::to_string(frame.line);
					}
					ImGui::BeginDisabled(!jumpable);
					if (ImGui::Selectable(frameLabel.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
						if (jumpable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							ManagedIDELauncher::OpenFile(frame.file, frame.line, frame.column > 0 ? frame.column : 1);
						}
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::TreePop();
			}

			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
