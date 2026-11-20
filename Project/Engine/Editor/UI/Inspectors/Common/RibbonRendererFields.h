#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticleMaterialResolver.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine::RibbonRendererFields {

	inline ValueEditResult DrawMaterial(const EditorPanelContext& context, AssetID& id, bool& invalid, AssetID defaultMaterial) {

		AssetEditSetting material{};
		material.defaultAssetID = defaultMaterial;
		AssetID selected = id == defaultMaterial ? AssetID{} : id;
		const auto edit = MyGUI::AssetReferenceField("マテリアル", selected, context.editorContext->assetDatabase, { AssetType::Material }, material);
		if (edit.valueChanged) {
			invalid = false;
			if (selected && selected != material.defaultAssetID) {
				const auto asset = ParticleMaterialResolver::LoadMaterialAsset(context, selected);
				const auto* pass = asset ? FindPass(*asset, MaterialPassKind::Transparent) : nullptr;
				invalid = !pass || !IsTrailMaterialPass(*asset, *pass);
			}
			if (!invalid) id = selected ? selected : defaultMaterial;
		}
		if (invalid) MyGUI::Text("Trail用のマテリアルを指定してください");
		return invalid ? ValueEditResult{} : edit;
	}

	template<class Curve, class Field>
	void DrawCurve(const char* label, Curve& curve, CurveEditorState& state, Field&& field) {

		if (!MyGUI::CollapsingHeader(label, false)) return;
		const CurveEditSetting options{ .fixedTimeRange = true };
		const auto result = MyGUI::CurveEditor(label, curve, state, options);
		field({result.valueChanged, result.anyItemActive, result.editFinished});
		if constexpr (std::is_same_v<Curve, CurveColor4>) {
			MyGUI::CurveColorGradientBar(curve.channels, 0.0f, 1.0f, true);
		}
	}
}
