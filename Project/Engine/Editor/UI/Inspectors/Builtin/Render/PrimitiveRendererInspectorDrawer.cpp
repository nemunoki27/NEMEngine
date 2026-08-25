#include "PrimitiveRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <filesystem>
#include <variant>
#include <vector>

//============================================================================
//	PrimitiveRendererInspectorDrawer internal
//============================================================================
namespace {

	// 描画空間に応じた既定マテリアルを返す
	Engine::AssetID EffectiveDefaultMaterial(const Engine::PrimitiveRendererComponent& component) {

		return Engine::IsPrimitiveScreen2D(component) ?
			Engine::DefaultMaterialSettings::GetInstance().GetPrimitive2DOrBuiltin() :
			Engine::DefaultMaterialSettings::GetInstance().GetPrimitiveOrBuiltin();
	}

}

//============================================================================
//	PrimitiveRendererInspectorDrawer classMethods
//============================================================================
void Engine::PrimitiveRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	DrawField(anyItemActive, [&]() {
		return InspectorDrawerCommon::DrawCheckboxField("表示", draft.visible);
		});

	// 形状の選択
	{
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("形状", draft.type);
			});
	}
	// 描画空間、Plane/Ringのみ2D描画に切り替えられる
	if (draft.type == PrimitiveType::Plane || draft.type == PrimitiveType::Ring) {
		DrawField(anyItemActive, [&]() {
			return InspectorDrawerCommon::DrawEnumComboField("描画空間", draft.renderSpace);
			});
	}
	// マテリアル
	{
		DrawField(anyItemActive, [&]() {
			AssetEditSetting setting{};
			setting.defaultAssetID = EffectiveDefaultMaterial(draft);
			return MyGUI::AssetReferenceField("マテリアル", draft.material,
				context.editorContext->assetDatabase, { AssetType::Material }, setting);
			});
	}
	// 描画パラメータ
	InspectorDrawerCommon::DrawCommonRenderFields(
		[&](auto&& f) { DrawField(anyItemActive, std::forward<decltype(f)>(f)); },
		draft.layer, draft.order, draft.blendMode, draft.queue,
		&draft.renderingLayerMask);
	// 影/反射などのフラグ
	{
		const auto drawFlag = [&](const char* label, MeshRenderFlags flag) {
			DrawField(anyItemActive, [&]() {
				bool value = HasMeshRenderFlag(draft.renderFlags, flag);
				ValueEditResult result = InspectorDrawerCommon::DrawCheckboxField(label, value);
				if (result.valueChanged) {
					SetMeshRenderFlag(draft.renderFlags, flag, value);
				}
				return result;
				});
			};
		drawFlag("ライティング", MeshRenderFlags::Lighting);
		drawFlag("影を落とす", MeshRenderFlags::CastShadow);
		drawFlag("影を受ける", MeshRenderFlags::ReceiveShadow);
		drawFlag("反射に映る", MeshRenderFlags::CastReflection);
		drawFlag("反射を受ける", MeshRenderFlags::ReceiveReflection);
	}

	ImGui::SeparatorText("形状別パラメータ");

	// 形状ごとのパラメータ
	switch (draft.type) {
	case PrimitiveType::Plane:
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("大きさ", draft.plane.size, { .dragSpeed = 0.01f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("基準点", draft.plane.pivot, { .dragSpeed = 0.01f }); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("軸", draft.plane.axis); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("分割X", draft.plane.divideX, { .minValue = 1,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("分割Y", draft.plane.divideY, { .minValue = 1,.maxValue = kMaxPrimitiveDivide }); });
		break;
	case PrimitiveType::CrossPlane:
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("大きさ", draft.crossPlane.size, { .dragSpeed = 0.01f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector2("基準点", draft.crossPlane.pivot, { .dragSpeed = 0.01f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("枚数", draft.crossPlane.planeCount, { .minValue = 2,.maxValue = 8 }); });
		break;
	case PrimitiveType::Ring:
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("外周半径", draft.ring.outerRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("内周半径", draft.ring.innerRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("開始角", draft.ring.startAngle, { .dragSpeed = 0.5f,.minValue = 0.0f,.maxValue = 360.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("終了角", draft.ring.endAngle, { .dragSpeed = 0.5f,.minValue = 0.0f,.maxValue = 360.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("分割数", draft.ring.divide, { .minValue = 3,.maxValue = kMaxPrimitiveDivide }); });
		break;
	case PrimitiveType::Cylinder:
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("上面半径", draft.cylinder.topRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("中心半径", draft.cylinder.centerRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("下面半径", draft.cylinder.bottomRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("上面Weight", draft.cylinder.topRadiusWeight, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("下面Weight", draft.cylinder.bottomRadiusWeight, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 1.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::ColorEdit("上面色", draft.cylinder.topColor); });
		DrawField(anyItemActive, [&]() { return MyGUI::ColorEdit("中心色", draft.cylinder.centerColor); });
		DrawField(anyItemActive, [&]() { return MyGUI::ColorEdit("底面色", draft.cylinder.bottomColor); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("高さ", draft.cylinder.height, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("展開角", draft.cylinder.maxAngle, { .dragSpeed = 0.5f,.minValue = 0.0f,.maxValue = 360.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("円周分割", draft.cylinder.radialDivide, { .minValue = 3,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("高さ分割", draft.cylinder.heightDivide, { .minValue = 2,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("フタ", draft.cylinder.cap); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawEnumComboField("UVモード", draft.cylinder.uvMode); });
		break;
	case PrimitiveType::Sphere:
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("半径", draft.sphere.radius, { .dragSpeed = 0.01f,.minValue = 0.001f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("経度分割", draft.sphere.longitudeDivide, { .minValue = 3,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("緯度分割", draft.sphere.latitudeDivide, { .minValue = 2,.maxValue = kMaxPrimitiveDivide }); });
		break;
	case PrimitiveType::Hemisphere:
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("半径", draft.hemisphere.radius, { .dragSpeed = 0.01f,.minValue = 0.001f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("経度分割", draft.hemisphere.longitudeDivide, { .minValue = 3,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("緯度分割", draft.hemisphere.latitudeDivide, { .minValue = 2,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return InspectorDrawerCommon::DrawCheckboxField("底面のフタ", draft.hemisphere.bottomCap); });
		break;
	case PrimitiveType::Cube:
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("大きさ", draft.cube.size, { .dragSpeed = 0.01f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragVector3("基準点", draft.cube.pivot, { .dragSpeed = 0.01f }); });
		break;
	}

	// マテリアルのPBRパラメータをシェーダーreflection駆動で編集する
	DrawReflectedParameters(context, draft, anyItemActive);
}

const Engine::ShaderReflectionInfo* Engine::PrimitiveRendererInspectorDrawer::EnsureMaterialReflection(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID) {

	if (!context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) {
		return nullptr;
	}
	// 空マテリアルは描画時にデフォルトへ解決されるので、reflectionも実効デフォルトから引く
	if (!materialID) {
		materialID = defaultMaterialID;
	}
	// マテリアルが変わったときだけファイルを読み直す
	if (!cachedMaterialValid_ || cachedMaterialID_ != materialID) {

		cachedMaterialValid_ = false;
		cachedMaterialID_ = materialID;
		cachedMaterial_ = MaterialAsset{};
		const std::filesystem::path path = context.editorContext->assetDatabase->ResolveFullPath(materialID);
		if (!path.empty()) {

			nlohmann::json data = JsonAdapter::Load(path.string(), false);
			cachedMaterialValid_ = FromJson(data, cachedMaterial_);
		}
	}
	if (!cachedMaterialValid_) {
		return nullptr;
	}
	return context.renderPipeline->FindMaterialDrawReflection(cachedMaterial_);
}

Engine::MaterialParameterValue Engine::PrimitiveRendererInspectorDrawer::ResolveParamValue(
	const PrimitiveRendererComponent& draft, const ShaderConstantBufferVariable& var) const {

	if (const MaterialParameterValue* value =
		draft.materialInstance.Find(var.parameterID)) {

		return *value;
	}
	if (var.semantic != MaterialParameterSemantic::None) {

		if (const MaterialParameterValue* value =
			draft.materialInstance.Find(var.semantic)) {

			return *value;
		}
	}
	if (const MaterialParameterValue* value =
		cachedMaterial_.parameters.Find(var.parameterID)) {

		return *value;
	}
	if (var.semantic != MaterialParameterSemantic::None) {

		if (const MaterialParameterValue* value =
			cachedMaterial_.parameters.Find(var.semantic)) {

			return *value;
		}
	}
	return MaterialParameterEditor::DefaultValueForVariable(var);
}

void Engine::PrimitiveRendererInspectorDrawer::DrawReflectedParameters(
	const EditorPanelContext& context, PrimitiveRendererComponent& draft, bool& anyItemActive) {

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, draft.material, EffectiveDefaultMaterial(draft));
	if (!reflection) {
		return;
	}
	MaterialParameterLayout layout{};
	layout.Build(*reflection, MaterialParameterCBuffer::kSurface);
	if (!layout.IsValid()) {
		return;
	}

	ImGui::SeparatorText("シェーダーパラメータ");

	// ID順のランタイムレイアウトとは分離し、標準PBRの編集順で表示する
	std::vector<const ShaderConstantBufferVariable*> scalarVariables;
	std::vector<const ShaderConstantBufferVariable*> textureVariables;
	for (const ShaderConstantBufferVariable& var : layout.GetVariables()) {

		if (!var.used ||
			MaterialParameterEditor::IsInternalPaddingParameter(var)) {
			continue;
		}
		if (MaterialParameterEditor::IsReflectedTextureParam(
			var, *reflection)) {
			textureVariables.emplace_back(&var);
		} else {
			scalarVariables.emplace_back(&var);
		}
	}
	MaterialParameterEditor::SortScalarParametersForDisplay(
		scalarVariables);
	MaterialParameterEditor::SortTextureParametersForDisplay(
		textureVariables);

	for (const ShaderConstantBufferVariable* var : scalarVariables) {

		MaterialParameterValue value = ResolveParamValue(draft, *var);
		const FloatEditSetting floatSetting{};
		DrawField(anyItemActive, [&]() {

			// valueChangedでプレビュー更新、editFinishedでcommitされUndo/dirtyに乗る
			ValueEditResult result = MaterialParameterEditor::DrawValueEdit(*var, value, floatSetting);
			if (result.valueChanged) {
				draft.materialInstance.Set(var->parameterID,
					var->name, var->semantic, value);
			}
			return result;
			});
	}

	const auto drawTexture = [&](MaterialParameterID parameterID,
		MaterialParameterSemantic semantic, std::string_view displayName,
		AssetID textureID) {

		DrawField(anyItemActive, [&]() {

			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			ValueEditResult result = MyGUI::AssetReferenceField(
				displayName.data(), textureID,
				context.editorContext->assetDatabase,
				{ AssetType::Texture }, setting);
			if (result.valueChanged) {
				MaterialParameterValue value{};
				value.value = textureID;
				draft.materialInstance.Set(
					parameterID, displayName, semantic, value);
			}
			return result;
			});
		};

	for (const ShaderConstantBufferVariable* variable : textureVariables) {
		AssetID textureID{};
		const MaterialParameterValue value =
			ResolveParamValue(draft, *variable);
		if (const AssetID* resolved =
			std::get_if<AssetID>(&value.value)) {
			textureID = *resolved;
		}
		drawTexture(variable->parameterID, variable->semantic,
			variable->name, textureID);
	}

	// space2のテクスチャSRVをレンダラー個別の上書きとして下にまとめて出す、表示順を整える
	std::vector<const ShaderResourceBinding*> textures;
	for (const ShaderResourceBinding& resource : reflection->resources) {

		if (!MaterialParameterEditor::IsMaterialTextureResource(resource)) {
			continue;
		}
		if (const ShaderConstantBufferVariable* variable =
			MaterialParameterEditor::FindReflectedTextureParameter(
				resource, *reflection)) {
			if (MaterialParameterEditor::IsReflectedTextureParam(
				*variable, *reflection)) {
				continue;
			}
		}
		textures.push_back(&resource);
	}
	std::stable_sort(textures.begin(), textures.end(),
		[](const ShaderResourceBinding* lhs, const ShaderResourceBinding* rhs) {
			return MaterialParameterEditor::GetTextureDisplayRank(
				lhs->semantic, lhs->name) <
				MaterialParameterEditor::GetTextureDisplayRank(
					rhs->semantic, rhs->name);
		});

	for (const ShaderResourceBinding* resource : textures) {

		const MaterialParameterID parameterID =
			MaterialParameterEditor::GetReflectedTextureParameterID(
				*resource, *reflection);
		const MaterialParameterSemantic semantic =
			MaterialParameterEditor::GetReflectedTextureSemantic(
				*resource, *reflection);
		const std::string_view displayName =
			MaterialParameterEditor::GetReflectedTextureDisplayName(
				*resource, *reflection);
		const auto resolveTexture = [parameterID, semantic, displayName](
			const MaterialParameterSet& parameters) -> AssetID {

			const MaterialParameterValue* parameter =
				parameters.Find(parameterID);
			if (!parameter && semantic != MaterialParameterSemantic::None) {
				parameter = parameters.Find(semantic);
			}
			if (!parameter) {
				parameter = parameters.FindByName(displayName);
			}
			if (parameter) {
				if (const AssetID* textureID =
					std::get_if<AssetID>(&parameter->value)) {
					return *textureID;
				}
			}
			return AssetID{};
			};
		AssetID textureID = resolveTexture(draft.materialInstance);
		if (!textureID) {
			textureID = resolveTexture(cachedMaterial_.parameters);
		}
		drawTexture(parameterID, semantic, displayName, textureID);
	}
}
