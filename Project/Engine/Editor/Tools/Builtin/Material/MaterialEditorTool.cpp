#include "MaterialEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>

// imgui
#include <imgui.h>

//============================================================================
//	MaterialEditorTool classMethods
//============================================================================
void Engine::MaterialEditorTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::MaterialEditorTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::MaterialEditorTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("Material", &openWindow_)) {
		ImGui::End();
		return;
	}

	DrawDefaultMaterialSection(context);

	ImGui::End();
}

void Engine::MaterialEditorTool::DrawDefaultMaterialSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	ImGui::SeparatorText("デフォルトマテリアル");
	ImGui::TextWrapped("各描画タイプの空マテリアルが解決される既定を設定する 未設定はbuiltinが使われる");

	DefaultMaterialSettings& settings = DefaultMaterialSettings::GetInstance();

	// マテリアルを差し替えたら即保存する、空に戻すとbuiltinへ戻る
	// 未設定でも実効のbuiltin名をDefault表示するため設定済みのbuiltin込み実効値を渡す
	auto drawSlot = [&](const char* label, AssetID current, AssetID effectiveDefault, auto&& setter) {

		AssetID value = current;
		AssetEditSetting setting{};
		setting.defaultAssetID = effectiveDefault;
		if (MyGUI::AssetReferenceField(label, value, assetDatabase, { AssetType::Material }, setting).valueChanged) {

			setter(value);
			settings.Save();
		}
		};

	drawSlot("Mesh", settings.GetMesh(), settings.GetMeshOrBuiltin(),
		[&](AssetID id) { settings.SetMesh(id); });
	drawSlot("Sprite", settings.GetSprite(), settings.GetSpriteOrBuiltin(),
		[&](AssetID id) { settings.SetSprite(id); });
	drawSlot("Text", settings.GetText(), settings.GetTextOrBuiltin(),
		[&](AssetID id) { settings.SetText(id); });
}
