#include "ShaderGraphToolbar.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

void Engine::ShaderGraphToolbar::Draw(
	const EditorToolContext& context, ShaderGraphEditSession& session, IShaderGraphToolbarActions& actions) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphToolbar");
	AssetID selected = session.GetAssetID();
	if (MyGUI::AssetReferenceField("グラフ", selected, assetDatabase, {AssetType::ShaderGraph}).valueChanged) {

		actions.RequestGraphSwitch(context, selected);
	}

	AssetID importSource{};
	AssetEditSetting importSetting{};
	importSetting.allowDelete = false;
	ImGui::BeginDisabled(!session.IsLoaded());
	if (MyGUI::AssetReferenceField(
			"設定をインポート", importSource, assetDatabase, {AssetType::Material, AssetType::ShaderGraph}, importSetting)
			.valueChanged) {
		actions.ImportGraphSettings(context, importSource);
	}
	ImGui::EndDisabled();

	MyGUI::EnumCombo("作成種類", creation_.domain);
	if (creation_.domain == ShaderGraphDomain::Surface) {
		MyGUI::EnumCombo("作成対象", creation_.target);
	}
	MyGUI::InputText("作成先", creation_.path);
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("新規作成", ImVec2(buttonWidth, 0.0f))) {
		actions.RequestGraphCreation(context);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!session.IsLoaded());
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		actions.SaveGraph(context);
	}
	ImGui::SameLine();
	if (ImGui::Button("保存してコンパイル", ImVec2(buttonWidth, 0.0f))) {

		actions.SaveAndCompile(context);
	}
	ImGui::EndDisabled();

	const float historyButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	ImGui::BeginDisabled(!session.IsLoaded() || !session.CanUndo());
	if (ImGui::Button("元に戻す", ImVec2(historyButtonWidth, 0.0f))) {
		actions.UndoGraph();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!session.IsLoaded() || !session.CanRedo());
	if (ImGui::Button("やり直す", ImVec2(historyButtonWidth, 0.0f))) {
		actions.RedoGraph();
	}
	ImGui::EndDisabled();

	if (!session.GetStatusMessage().empty()) {
		ImGui::TextUnformatted(session.GetStatusMessage().c_str());
	} else {
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
	}
}
