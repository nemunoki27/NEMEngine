#include "RendererRenderSettingsFields.h"

//============================================================================
//	include
//============================================================================
#include "InspectorDrawerCommon.h"

// c++
#include <algorithm>

namespace {

	Engine::ValueEditResult DrawByteField(const char* label, uint8_t& value) {

		int32_t number = value;
		auto result = Engine::MyGUI::DragInt(label, number, { .minValue = 0, .maxValue = 255 });
		if (result.valueChanged) value = static_cast<uint8_t>(std::clamp(number, 0, 255));
		return result;
	}

	void DrawStencilFace(Engine::ValueEditResult& result, Engine::RendererStencilFace& face) {

		using namespace Engine;
		using InspectorDrawerCommon::DrawFieldEdit;
		DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("ステンシル比較", face.comparison); });
		DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("ステンシル失敗時", face.fail); });
		DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("深度失敗時", face.depthFail); });
		DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("成功時", face.pass); });
	}
}

//============================================================================
//	RendererRenderSettingsFields functions
//============================================================================
Engine::ValueEditResult Engine::DrawRendererRenderSettingsFields(RendererRenderSettings& settings, bool* enableZPrepass) {

	ValueEditResult result;
	using InspectorDrawerCommon::DrawFieldEdit;
	ImGui::PushID("RendererRenderSettings");
	DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("マテリアルから継承", settings.inheritMaterial); });
	if (enableZPrepass) {
		DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("Zプリパス", *enableZPrepass); });
	}
	if (settings.inheritMaterial) MyGUI::Text("個別設定の保存値");
	ImGui::BeginDisabled(settings.inheritMaterial);
	DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("描画モード", settings.fillMode); });
	ImGui::BeginDisabled(!settings.depthTest);
	DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("深度書き込み", settings.depthWrite); });
	ImGui::EndDisabled();
	ImGui::BeginDisabled(!settings.depthTest);
	DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("深度比較", settings.depthComparison); });
	ImGui::EndDisabled();
	DrawFieldEdit(result, [&] { return MyGUI::EnumCombo("カリング", settings.cullMode); });
	DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("深度テスト", settings.depthTest); });
	DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("ステンシル設定", settings.stencilEnabled); });
	if (settings.stencilEnabled && ImGui::CollapsingHeader("ステンシル詳細")) {
		ImGui::Indent();
		DrawFieldEdit(result, [&] { return DrawByteField("参照値", settings.stencilReference); });
		DrawFieldEdit(result, [&] { return DrawByteField("読み取りマスク", settings.stencilReadMask); });
		DrawFieldEdit(result, [&] { return DrawByteField("書き込みマスク", settings.stencilWriteMask); });
		DrawFieldEdit(result, [&] { return MyGUI::CheckboxField("表裏を個別設定", settings.separateBackFace); });
		if (settings.separateBackFace) ImGui::SeparatorText("表面");
		ImGui::PushID("FrontFace");
		DrawStencilFace(result, settings.frontFace);
		ImGui::PopID();
		if (settings.separateBackFace) {
			ImGui::SeparatorText("裏面");
			ImGui::PushID("BackFace");
			DrawStencilFace(result, settings.backFace);
			ImGui::PopID();
		}
		ImGui::Unindent();
	}
	ImGui::EndDisabled();
	ImGui::PopID();
	return result;
}
