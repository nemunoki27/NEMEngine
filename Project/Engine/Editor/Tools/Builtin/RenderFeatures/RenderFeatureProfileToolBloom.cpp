#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Bloom/BloomSettings.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <array>

bool Engine::RenderFeatureProfileTool::DrawBloomAnchor(RenderFeatureAnchor& anchor) {

	// 透明描画とHDR合成が完了する位置だけを選ぶ
	static const std::vector<std::string> labels{"透明描画後", "ポストプロセスUI後", "最終出力前"};
	const std::array anchors{RenderFeatureAnchor::AfterTransparent, RenderFeatureAnchor::AfterPostProcessUI, RenderFeatureAnchor::BeforeBlit};
	std::string selected = labels[0];
	for (size_t i = 0; i < anchors.size(); ++i) if (anchor == anchors[i]) selected = labels[i];
	if (!MyGUI::StringCombo("実行位置", selected, labels).valueChanged) return false;
	for (size_t i = 0; i < anchors.size(); ++i) if (selected == labels[i]) anchor = anchors[i];
	return true;
}

void Engine::RenderFeatureProfileTool::DrawBloomSettings(const EditorToolContext& context,
	const RenderFeaturePassSettings& pass, MaterialParameterSet& parameters) {

	auto& overrides = RenderFeatureRuntimeOverrides::GetInstance();
	const auto* runtime = context.IsPlaying() ? overrides.Find(pass.id) : nullptr;
	BloomSettings settings = ReadBloomSettings(parameters, runtime ? &runtime->parameters : nullptr);
	// GPUの内部段階は隠し、見た目の設定だけを編集する
	bool changed = MyGUI::DragFloat("強度", settings.intensity, {.minValue = 0.0f}).valueChanged;
	changed |= MyGUI::DragFloat("輝度閾値", settings.threshold, {.minValue = 0.0f}).valueChanged;
	changed |= MyGUI::DragFloat("閾値の柔らかさ", settings.softKnee, {.minValue = 0.0f, .maxValue = 1.0f}).valueChanged;
	changed |= MyGUI::DragFloat("光の広がり", settings.scatter, {.minValue = 0.0f, .maxValue = 1.0f}).valueChanged;
	changed |= MyGUI::ColorEdit("色", settings.tint, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha).valueChanged;
	if (!changed) return;

	if (context.IsPlaying()) {
		// 保存値を変更せず、Stopで破棄する上書きへ渡す
		MaterialParameterSet edited;
		WriteBloomSettings(edited, settings);
		for (const auto& record : edited.GetRecords()) {
			overrides.SetParameter(pass.id, record.id, record.namedValue.first, record.namedValue.second);
		}
	} else {
		WriteBloomSettings(parameters, settings);
		editSession_.SetDirty();
	}
}
