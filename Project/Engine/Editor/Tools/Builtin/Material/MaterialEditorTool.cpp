#include "MaterialEditorTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <filesystem>
#include <optional>
#include <string>

#include <imgui.h>
#include <json.hpp>

//============================================================================
//	MaterialEditorTool internal
//============================================================================

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

	// このウィンドウだけ文字を少し小さくする、最後に戻す
	ImGui::SetWindowFontScale(0.8f);

	// タイプ既定値は初回だけ現タイプで埋める
	if (!typeDefaultsInitialized_) {
		creationSession_.ApplyTypeDefaults(creationSession_.GetDraft().createType);
		typeDefaultsInitialized_ = true;
	}

	// 役割ごとにタブで分ける
	if (ImGui::BeginTabBar("##MaterialTabs")) {

		if (ImGui::BeginTabItem("デフォルトマテリアル設定")) {

			DrawDefaultMaterialSection(context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("マテリアル作成")) {

			DrawCreateMaterialSection(context);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	// 他ウィンドウへ影響しないよう倍率を戻す
	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

void Engine::MaterialEditorTool::DrawDefaultMaterialSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

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
	drawSlot("Line", settings.GetLine(), settings.GetLineOrBuiltin(),
		[&](AssetID id) { settings.SetLine(id); });
	drawSlot("Primitive", settings.GetPrimitive(), settings.GetPrimitiveOrBuiltin(),
		[&](AssetID id) { settings.SetPrimitive(id); });
	drawSlot("Primitive2D", settings.GetPrimitive2D(), settings.GetPrimitive2DOrBuiltin(),
		[&](AssetID id) { settings.SetPrimitive2D(id); });
	drawSlot(
		"Raytracing Reflection",
		settings.GetRaytracingReflection(),
		settings.GetRaytracingReflectionOrBuiltin(),
		[&](AssetID id) {
			settings.SetRaytracingReflection(id);
		});
}

void Engine::MaterialEditorTool::DrawCreateMaterialSection(const EditorToolContext& context) {

	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;

	// 作成対象タイプ、切り替えで既定設定を入れ直す
	if (MyGUI::EnumCombo("タイプ", creationSession_.GetDraft().createType).valueChanged) {

		creationSession_.ApplyTypeDefaults(creationSession_.GetDraft().createType);
	}

	// 対象シェーダー、ParticleはPSだけを選び形状ステージをRendererに任せる
	ImGui::Separator();
	{
		AssetEditSetting setting{};
		if (creationSession_.GetDraft().createType != MaterialCreateType::Particle) {
			MyGUI::AssetReferenceField("VertexShader 必須", creationSession_.GetDraft().createVS, assetDatabase, { AssetType::Shader }, setting);
		}
		MyGUI::AssetReferenceField(creationSession_.GetDraft().createType == MaterialCreateType::Mesh ?
			"不透明PixelShader 必須" : "PixelShader 必須", creationSession_.GetDraft().createPS, assetDatabase, { AssetType::Shader }, setting);
		MyGUI::InputText("PSエントリー", creationSession_.GetDraft().createPSEntry);
		if (creationSession_.GetDraft().createType == MaterialCreateType::Mesh) {

			MyGUI::AssetReferenceField("MeshShader 任意", creationSession_.GetDraft().createMS, assetDatabase, { AssetType::Shader }, setting);
			MyGUI::AssetReferenceField("AmplificationShader 任意", creationSession_.GetDraft().createAS, assetDatabase, { AssetType::Shader }, setting);
			MyGUI::Checkbox("半透明パスを作成", creationSession_.GetDraft().createTransparentPass);
			if (creationSession_.GetDraft().createTransparentPass) {
				MyGUI::AssetReferenceField("半透明PixelShader 必須", creationSession_.GetDraft().createTransparentPS,
					assetDatabase, { AssetType::Shader }, setting);
				MyGUI::InputText("半透明PSエントリー", creationSession_.GetDraft().createTransparentPSEntry);
			}
		}
		// Lineは太線展開のGeometryShaderが必須
		if (creationSession_.GetDraft().createType == MaterialCreateType::Line) {

			MyGUI::AssetReferenceField("GeometryShader 必須", creationSession_.GetDraft().createGS, assetDatabase, { AssetType::Shader }, setting);
		}

		// 既存マテリアルをドロップするとそのパイプライン設定を下の編集欄へ取り込む
		AssetID sourceMaterial = creationSession_.GetDraft().createSourceMaterial;
		if (MyGUI::AssetReferenceField("設定取り込み元 任意", sourceMaterial,
			assetDatabase, { AssetType::Material }, setting).valueChanged) {

			creationSession_.GetDraft().createSourceMaterial = sourceMaterial;
			creationSession_.GetDraft().createSourceUsesShaderGraph = false;
			if (sourceMaterial && assetDatabase) {

				creationSession_.LoadPipelineSettingsFromMaterial(*assetDatabase, sourceMaterial);
				if (creationSession_.GetDraft().createImportShaders) {
					creationSession_.LoadShadersFromMaterial(*assetDatabase, sourceMaterial);
				}
			}
		}

		// 参照シェーダーも取り込む、後からONにしても取り込み元から引き直す
		if (MyGUI::Checkbox("参照シェーダーを復元", creationSession_.GetDraft().createImportShaders)) {
			if (creationSession_.GetDraft().createImportShaders && creationSession_.GetDraft().createSourceMaterial && assetDatabase) {
				creationSession_.LoadShadersFromMaterial(*assetDatabase, creationSession_.GetDraft().createSourceMaterial);
			}
		}
	}

	// 描画パスごとのPipeline設定を同じUIで編集する
	MyGUI::EnumCombo("表面方式", creationSession_.GetDraft().createPipeline.surfaceMode);
	auto drawPipelineSettings = [&](const char* id, const char* title, PipelineCreateSettings& settings) {
		ImGui::SeparatorText(title);
		ImGui::PushID(id);
		MyGUI::EnumCombo("ブレンド", settings.blendMode);
		MyGUI::EnumCombo("塗りモード", settings.fillMode);
		MyGUI::EnumCombo("カリング", settings.cullMode);
		MyGUI::Checkbox("前面反時計回り", settings.frontCounterClockwise);
		MyGUI::Checkbox("深度クリップ", settings.depthClipEnable);
		MyGUI::Checkbox("深度テスト", settings.depthEnable);
		MyGUI::EnumCombo("深度書込み", settings.depthWriteMask);
		MyGUI::EnumCombo("深度比較", settings.depthFunc);
		MyGUI::Checkbox("ステンシル", settings.stencilEnable);
		MyGUI::EnumCombo("フィルタ", settings.samplerFilter);
		MyGUI::EnumCombo("アドレスU", settings.samplerAddressU);
		MyGUI::EnumCombo("アドレスV", settings.samplerAddressV);
		MyGUI::EnumCombo("アドレスW", settings.samplerAddressW);
		MyGUI::EnumCombo("比較関数", settings.samplerComparison);
		MyGUI::EnumCombo("境界色", settings.samplerBorderColor);
		MyGUI::DragInt("異方性", settings.samplerMaxAnisotropy, {
			.dragSpeed = 1.0f,
			.minValue = 1,
			.maxValue = 16,
			});
		MyGUI::DragFloat("Mip LODバイアス", settings.samplerMipLODBias);
		MyGUI::DragFloat("最小LOD", settings.samplerMinLOD);
		MyGUI::DragFloat("最大LOD", settings.samplerMaxLOD);
		ImGui::PopID();
	};
	if (creationSession_.GetDraft().createSourceUsesShaderGraph) {
		ImGui::TextDisabled("Shader Graph Materialの描画設定はShader Graph側で編集します");
	}
	ImGui::BeginDisabled(creationSession_.GetDraft().createSourceUsesShaderGraph);
	drawPipelineSettings("DrawPipeline", creationSession_.GetDraft().createType == MaterialCreateType::Mesh ?
		"不透明パイプライン設定" : "パイプライン設定", creationSession_.GetDraft().createPipeline);
	if (creationSession_.GetDraft().createType == MaterialCreateType::Mesh && creationSession_.GetDraft().createTransparentPass) {
		drawPipelineSettings("TransparentPipeline", "半透明パイプライン設定", creationSession_.GetDraft().createTransparentPipeline);
	}
	ImGui::EndDisabled();

	// 出力先、GameAssets/Materials/固定でそれ以降をファイル名込みで入力する
	ImGui::SeparatorText("作成");
	MyGUI::InputText("GameAssets/Materials/", creationSession_.GetDraft().createRelativePath);

	const bool needsVS = creationSession_.GetDraft().createType != MaterialCreateType::Particle;
	const bool needsTransparentPS = creationSession_.GetDraft().createType == MaterialCreateType::Mesh && creationSession_.GetDraft().createTransparentPass;
	const bool canCreate = (!needsVS || static_cast<bool>(creationSession_.GetDraft().createVS)) &&
		static_cast<bool>(creationSession_.GetDraft().createPS) && (!needsTransparentPS || static_cast<bool>(creationSession_.GetDraft().createTransparentPS)) &&
		!creationSession_.GetDraft().createPSEntry.empty() && (!needsTransparentPS || !creationSession_.GetDraft().createTransparentPSEntry.empty()) &&
		!creationSession_.GetDraft().createRelativePath.empty();
	ImGui::BeginDisabled(!canCreate);
	if (ImGui::Button("Create", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		creationSession_.CreateMaterialAssets(context);
	}
	ImGui::EndDisabled();

	if (!creationSession_.GetDraft().createMessage.empty()) {
		ImGui::TextWrapped("%s", creationSession_.GetDraft().createMessage.c_str());
	}
}
