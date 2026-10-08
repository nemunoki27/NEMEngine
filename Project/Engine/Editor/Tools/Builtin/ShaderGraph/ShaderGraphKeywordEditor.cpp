#include "ShaderGraphKeywordEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <cfloat>
#include <string>
// imgui
#include <imgui.h>

void Engine::ShaderGraphKeywordEditor::Draw(ShaderGraphEditSession& session) {

	ImGui::SeparatorText("キーワード");
	for (uint32_t index = 0; index < session.GetDraft().keywords.size(); ++index) {
		ImGui::PushID(static_cast<int>(index));
		if (ImGui::Selectable(session.GetDraft().keywords[index].name.c_str(), selected_ == static_cast<int32_t>(index))) {
			selected_ = static_cast<int32_t>(index);
		}
		ImGui::PopID();
	}

	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("追加##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const uint32_t suffix = static_cast<uint32_t>(session.GetDraft().keywords.size() + 1);
		session.GetDraft().keywords.emplace_back(ShaderGraphKeyword{
			.id = UUID::New(), .name = "Keyword" + std::to_string(suffix), .referenceName = "KEYWORD_" + std::to_string(suffix),
		});
		selected_ = static_cast<int32_t>(session.GetDraft().keywords.size() - 1);
		session.MarkDirty();
	}
	ImGui::SameLine();
	const bool validSelection = selected_ >= 0 && static_cast<size_t>(selected_) < session.GetDraft().keywords.size();
	ImGui::BeginDisabled(!validSelection);
	if (ImGui::Button("削除##Keyword", ImVec2(buttonWidth, 0.0f))) {
		const UUID keywordID = session.GetDraft().keywords[static_cast<size_t>(selected_)].id;
		std::erase_if(session.GetDraft().nodes, [&](const ShaderGraphNode& node) {
			return node.kind == ShaderGraphNodeKind::Keyword && node.keywordID == keywordID;
		});
		std::erase_if(session.GetDraft().links, [&](const ShaderGraphLink& link) {
			return std::none_of(session.GetDraft().nodes.begin(), session.GetDraft().nodes.end(),
				[&](const ShaderGraphNode& node) { return node.id == link.outputNode; });
		});
		session.GetDraft().keywords.erase(session.GetDraft().keywords.begin() + selected_);
		selected_ = -1;
		session.MarkDirty();
	}
	ImGui::EndDisabled();
	if (!validSelection || selected_ < 0) {
		return;
	}

	ShaderGraphKeyword& keyword = session.GetDraft().keywords[static_cast<size_t>(selected_)];
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphKeywordSettings");
	session.MarkDirty(MyGUI::InputText("名前", keyword.name).valueChanged);
	session.MarkDirty(MyGUI::InputText("参照名", keyword.referenceName).valueChanged);
	if (MyGUI::EnumCombo("型", keyword.type).valueChanged) {
		keyword.defaultIndex = 0;
		if (keyword.type == ShaderGraphKeywordType::Boolean) {
			keyword.entries.clear();
		}
		session.MarkDirty();
	}
	session.MarkDirty(MyGUI::Checkbox("実行時切り替え", keyword.runtimeToggle));
	if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(keyword.runtimeToggle ? "Material Instanceから値を変更する動的分岐"
													 : "既定値をHLSLへ埋め込み、保存時に再コンパイル");
		ImGui::EndTooltip();
	}
	if (keyword.type == ShaderGraphKeywordType::Boolean) {
		bool defaultValue = keyword.defaultIndex != 0;
		if (MyGUI::Checkbox("既定値", defaultValue)) {
			keyword.defaultIndex = defaultValue ? 1u : 0u;
			session.MarkDirty();
		}
		return;
	}

	int32_t defaultIndex = static_cast<int32_t>(keyword.defaultIndex);
	if (MyGUI::DragInt("既定値", defaultIndex, {
				.dragSpeed = 1.0f,
				.minValue = 0,
				.maxValue = (std::max)(static_cast<int32_t>(keyword.entries.size()) - 1, 0),
			})
			.valueChanged) {
		keyword.defaultIndex = static_cast<uint32_t>(defaultIndex);
		session.MarkDirty();
	}
	for (uint32_t index = 0; index < keyword.entries.size();) {
		ImGui::PushID(static_cast<int>(index));
		const std::string label = "値 " + std::to_string(index);
		session.MarkDirty(MyGUI::InputText(label.c_str(), keyword.entries[index]).valueChanged);
		ImGui::SameLine();
		if (ImGui::SmallButton("削除")) {
			keyword.entries.erase(keyword.entries.begin() + index);
			keyword.defaultIndex = (std::min)(keyword.defaultIndex,
				keyword.entries.empty() ? 0u : static_cast<uint32_t>(keyword.entries.size() - 1));
			session.MarkDirty();
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++index;
	}
	if (ImGui::Button("列挙値を追加", ImVec2(-FLT_MIN, 0.0f))) {
		keyword.entries.emplace_back("Value" + std::to_string(keyword.entries.size()));
		session.MarkDirty();
	}
}
