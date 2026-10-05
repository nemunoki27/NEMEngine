#include "ShaderGraphAppearanceEditor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

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
} // namespace

Engine::ShaderGraphAppearanceEditor::ShaderGraphAppearanceEditor() {

	// 保存済み設定を既定値へ重ねる
	ShaderGraphAppearance::RestoreDefaultAppearance(settings_);
	ShaderGraphAppearance::LoadAppearanceSettings(settings_);
}

bool Engine::ShaderGraphAppearanceEditor::Draw(std::string& status) {

	if (!MyGUI::CollapsingHeader("見た目設定", false)) {
		return false;
	}

	bool invalidatePreviews = false;
	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
	if (ImGui::Button("保存##Appearance", ImVec2(buttonWidth, 0.0f))) {

		ShaderGraphAppearance::SaveAppearanceSettings(settings_);
		status = "見た目設定を保存しました";
	}
	ImGui::SameLine();
	if (ImGui::Button("読み込み##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize = settings_.nodePreviewTextureSize;
		const bool loaded = ShaderGraphAppearance::LoadAppearanceSettings(settings_);
		if (loaded && previousTextureSize != settings_.nodePreviewTextureSize) {

			invalidatePreviews = true;
		}
		status = loaded ? "見た目設定を読み込みました" : "保存済みの見た目設定がありません";
	}
	ImGui::SameLine();
	if (ImGui::Button("元に戻す##Appearance", ImVec2(buttonWidth, 0.0f))) {

		const int32_t previousTextureSize = settings_.nodePreviewTextureSize;
		ShaderGraphAppearance::RestoreDefaultAppearance(settings_);
		if (previousTextureSize != settings_.nodePreviewTextureSize) {

			invalidatePreviews = true;
		}
		status = "見た目設定を既定値に戻しました";
	}

	if (MyGUI::CollapsingHeader("キャンバス", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphCanvasAppearance");
		MyGUI::ColorEdit("背景", settings_.canvasBackground);
		MyGUI::ColorEdit("グリッド", settings_.grid);
		MyGUI::DragFloat("グリッド間隔", settings_.gridSpacing, AppearanceFloatSetting(4.0f, 256.0f, 1.0f));
		MyGUI::DragFloat("ノードスナップ間隔", settings_.nodeSnapGridSize, AppearanceFloatSetting(0.0f, 256.0f, 1.0f));
	}
	if (MyGUI::CollapsingHeader("ノード", true)) {

		MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphNodeAppearance");
		MyGUI::ColorEdit("背景", settings_.nodeBackground);
		MyGUI::ColorEdit("境界線", settings_.nodeBorder);
		MyGUI::ColorEdit("ホバー境界線", settings_.hoveredNodeBorder);
		MyGUI::ColorEdit("選択境界線", settings_.selectedNodeBorder);
		MyGUI::ColorEdit("範囲選択", settings_.nodeSelection);
		MyGUI::ColorEdit("範囲選択境界線", settings_.nodeSelectionBorder);
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
		MyGUI::ColorEdit("リンク", settings_.link);
		MyGUI::ColorEdit("ホバーリンク", settings_.hoveredLinkBorder);
		MyGUI::ColorEdit("選択リンク", settings_.selectedLinkBorder);
		MyGUI::ColorEdit("強調リンク", settings_.highlightedLinkBorder);
		MyGUI::ColorEdit("リンク範囲選択", settings_.linkSelection);
		MyGUI::ColorEdit("リンク範囲境界線", settings_.linkSelectionBorder);
		MyGUI::ColorEdit("ピン範囲選択", settings_.pinSelection);
		MyGUI::ColorEdit("ピン範囲境界線", settings_.pinSelectionBorder);
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
