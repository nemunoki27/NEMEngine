#include "DecalMaterialFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>

namespace {

	// 変更した値だけを上書き集合へ戻す
	template<typename T, typename Drawer>
	void DrawValue(Engine::MaterialParameterSet& parameters, const Engine::MaterialParameterSet& defaults,
		const char* name, T fallback, Drawer&& drawer, Engine::ValueEditResult& total) {

		const auto id = Engine::MaterialParameterID::FromName(name);
		const auto* parameter = parameters.Find(id);
		if (!parameter) parameter = defaults.Find(id);
		const auto* stored = parameter ? std::get_if<T>(&parameter->value) : nullptr;
		T value = stored ? *stored : fallback;
		const auto menu = Engine::MaterialParameterEditor::MakeLabelContextMenu(id, name);
		const auto result = drawer(value);
		if (result.valueChanged) parameters.Set(name, Engine::MaterialParameterValue{.value = value});
		total.valueChanged |= result.valueChanged;
		total.editFinished |= result.editFinished;
		total.anyItemActive |= result.anyItemActive;
	}
}

Engine::ValueEditResult Engine::DecalMaterialFields::Draw(MaterialParameterSet& parameters,
	const MaterialParameterSet& defaults, const AssetDatabase* database) {

	ValueEditResult result;
	const auto checkbox = [&](const char* name, const char* label, bool fallback) {
		DrawValue(parameters, defaults, name, fallback,
			[&](bool& value) { return InspectorDrawerCommon::DrawCheckboxField(label, value); }, result);
	};
	const auto number = [&](const char* name, const char* label, float fallback, float maximum) {
		DrawValue(parameters, defaults, name, fallback,
			[&](float& value) { return MyGUI::DragFloat(label, value, {.minValue = 0.0f, .maxValue = maximum}); }, result);
	};
	const auto color = [&](const char* name, const char* label) {
		DrawValue(parameters, defaults, name, Color4::White(),
			[&](Color4& value) { return MyGUI::ColorEdit(label, value); }, result);
	};
	const auto texture = [&](const char* name, const char* label) {
		DrawValue(parameters, defaults, name, AssetID{},
			[&](AssetID& value) { return MyGUI::AssetReferenceField(label, value, database, {AssetType::Texture, AssetType::RenderTexture}); }, result);
	};
	// 色TextureのAlphaで全チャンネルの形状を決める
	checkbox("affectColor", "色を適用", true);
	color("color", "ベースカラー");
	texture("baseColorTexture", "ベースカラーテクスチャ");
	number("opacity", "不透明度", 1.0f, 1.0f);
	ImGui::Separator();
	checkbox("affectNormal", "法線を適用", false);
	texture("normalTexture", "法線テクスチャ");
	number("normalStrength", "法線の強さ", 1.0f, 8.0f);
	ImGui::Separator();
	checkbox("affectSurface", "質感を適用", false);
	number("metallic", "メタリック", 0.0f, 1.0f);
	number("roughness", "ラフネス", 0.5f, 1.0f);
	texture("metallicRoughnessTexture", "質感テクスチャ");
	ImGui::Separator();
	checkbox("affectEmission", "発光を適用", false);
	color("emissiveColor", "発光色");
	number("emissiveIntensity", "発光強度", 0.0f, 10000.0f);
	texture("emissiveTexture", "発光テクスチャ");
	return result;
}
