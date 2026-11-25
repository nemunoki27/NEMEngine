#include "ShaderGraphAppearanceEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/ImGui/EditorPalette.h>

// imgui
#include <imgui.h>

namespace {

	// 外観の数値編集に使う範囲を作る
	Engine::FloatEditSetting AppearanceFloatSetting(float minValue, float maxValue, float dragSpeed = 0.1f) {

		return Engine::FloatEditSetting{
			.dragSpeed = dragSpeed,
			.minValue = minValue,
			.maxValue = maxValue,
		};
	}

	void EditColor(const char* label, const char* key, std::string& status) {

		const auto current = Engine::EditorPalette::Get(key);
		Engine::Color4 color(current.x, current.y, current.z, current.w);
		ImGui::PushID(key);
		const auto edit = Engine::MyGUI::ColorEdit(label, color);
		ImGui::PopID();
		if (edit.valueChanged) Engine::EditorPalette::Set(key, ImVec4(color.r, color.g, color.b, color.a));
		if (edit.editFinished && !Engine::EditorPalette::SaveUser()) status = "個人の配色を保存できませんでした";
	}
} // namespace

Engine::ShaderGraphAppearanceEditor::ShaderGraphAppearanceEditor() {

	// 保存済み設定を既定値へ重ねる
	ShaderGraphAppearance::RestoreDefaultAppearance(settings_);
	ShaderGraphAppearance::LoadAppearanceSettings(settings_);
}

bool Engine::ShaderGraphAppearanceEditor::Draw(std::string& status) {

	bool invalidatePreviews = false;
	ImGui::TextUnformatted("レイアウト設定");
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("保存##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const bool saved = ShaderGraphAppearance::SaveAppearanceSettings(settings_);
		status = saved ? "レイアウト設定を保存しました" : "レイアウト設定の保存に失敗しました";
	}
	ImGui::SameLine();
	if (ImGui::Button("読み込み##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize = settings_.nodePreviewTextureSize;
		const bool loaded = ShaderGraphAppearance::LoadAppearanceSettings(settings_);
		if (loaded && previousTextureSize != settings_.nodePreviewTextureSize) {

			invalidatePreviews = true;
		}
		status = loaded ? "レイアウト設定を読み込みました" : "保存済みのレイアウト設定がありません";
	}
	ImGui::SameLine();
	if (ImGui::Button("元に戻す##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize = settings_.nodePreviewTextureSize;
		ShaderGraphAppearance::RestoreDefaultAppearance(settings_);
		if (previousTextureSize != settings_.nodePreviewTextureSize) {

			invalidatePreviews = true;
		}
		status = "レイアウト設定を既定値に戻しました";
	}

	if (MyGUI::CollapsingHeader("キャンバス", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphCanvasAppearance");
		EditColor("背景", "Graph.canvasBackground", status);
		EditColor("グリッド", "Graph.grid", status);
		MyGUI::DragFloat("グリッド間隔", settings_.gridSpacing, AppearanceFloatSetting(4.0f, 256.0f, 1.0f));
		MyGUI::DragFloat("ノードスナップ間隔", settings_.nodeSnapGridSize, AppearanceFloatSetting(0.0f, 256.0f, 1.0f));
	}
	if (MyGUI::CollapsingHeader("ノード", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphNodeAppearance");
		EditColor("背景", "Graph.nodeBackground", status);
		EditColor("境界線", "Graph.nodeBorder", status);
		EditColor("ホバー境界線", "Graph.hoveredNodeBorder", status);
		EditColor("選択境界線", "Graph.selectedNodeBorder", status);
		EditColor("範囲選択", "Graph.nodeSelection", status);
		EditColor("範囲選択境界線", "Graph.nodeSelectionBorder", status);
		MyGUI::DragVector4("余白", settings_.nodePadding, AppearanceFloatSetting(0.0f, 64.0f));
		MyGUI::DragFloat("文字スケール", settings_.nodeTextScale, AppearanceFloatSetting(0.5f, 2.0f, 0.01f));
		MyGUI::DragFloat("ノード最小幅", settings_.nodeMinimumWidth, AppearanceFloatSetting(0.0f, 0.0f, 1.0f));
		MyGUI::DragFloat("プレビュー表示サイズ", settings_.nodePreviewDisplaySize, AppearanceFloatSetting(64.0f, 512.0f, 1.0f));
		const ValueEditResult previewTextureSizeResult = MyGUI::DragInt("プレビュー解像度", settings_.nodePreviewTextureSize,
			{
				.dragSpeed = 1.0f,
				.minValue = 32,
				.maxValue = 1024,
			});
		if (previewTextureSizeResult.editFinished) {
			ShaderGraphAppearance::ClampAppearanceSettings(settings_);
			invalidatePreviews = true;
		}
		MyGUI::DragFloat("角丸", settings_.nodeRounding, AppearanceFloatSetting(0.0f, 32.0f));
		MyGUI::DragFloat("境界線幅", settings_.nodeBorderWidth, AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat("ホバー境界線幅", settings_.hoveredNodeBorderWidth, AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat("選択境界線幅", settings_.selectedNodeBorderWidth, AppearanceFloatSetting(0.0f, 10.0f));
	}
	if (MyGUI::CollapsingHeader("接続", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphConnectionAppearance");
		EditColor("リンク", "Graph.link", status);
		EditColor("ホバーリンク", "Graph.hoveredLinkBorder", status);
		EditColor("選択リンク", "Graph.selectedLinkBorder", status);
		EditColor("強調リンク", "Graph.highlightedLinkBorder", status);
		EditColor("リンク範囲選択", "Graph.linkSelection", status);
		EditColor("リンク範囲境界線", "Graph.linkSelectionBorder", status);
		EditColor("ピン範囲選択", "Graph.pinSelection", status);
		EditColor("ピン範囲境界線", "Graph.pinSelectionBorder", status);
		MyGUI::DragVector2("開始位置オフセット", settings_.linkStartOffset, AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragVector2("終了位置オフセット", settings_.linkEndOffset, AppearanceFloatSetting(-128.0f, 128.0f));
		MyGUI::DragFloat("ピン角丸", settings_.pinRounding, AppearanceFloatSetting(0.0f, 16.0f));
		MyGUI::DragFloat("ピン境界線幅", settings_.pinBorderWidth, AppearanceFloatSetting(0.0f, 10.0f));
		MyGUI::DragFloat("リンク曲率", settings_.linkStrength, AppearanceFloatSetting(0.0f, 500.0f, 1.0f));
		MyGUI::DragFloat("リンク太さ", settings_.linkThickness, AppearanceFloatSetting(0.1f, 10.0f));
	}
	ShaderGraphAppearance::ClampAppearanceSettings(settings_);
	return invalidatePreviews;
}

void Engine::ShaderGraphAppearanceEditor::Apply() const {

	ShaderGraphAppearance::ApplyAppearanceSettings(settings_);
}
