#include "PrimitiveRendererInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
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

	// テクスチャ欄の表示順、リストにない名前は末尾へ回す
	size_t TextureDisplayRank(const std::string& name) {

		static const char* kOrder[] = {
			"baseColorTexture", "normalTexture", "emissiveTexture", "metallicRoughnessTexture", "occlusionTexture",
		};
		for (size_t i = 0; i < std::size(kOrder); ++i) {
			if (name == kOrder[i]) {
				return i;
			}
		}
		return std::size(kOrder);
	}
}

//============================================================================
//	PrimitiveRendererInspectorDrawer classMethods
//============================================================================
void Engine::PrimitiveRendererInspectorDrawer::DrawFields(const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();

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
		draft.layer, draft.order, draft.visible, draft.blendMode, draft.queue);
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
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("下面半径", draft.cylinder.bottomRadius, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("高さ", draft.cylinder.height, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragFloat("展開角", draft.cylinder.maxAngle, { .dragSpeed = 0.01f,.minValue = 0.0f,.maxValue = 10000.0f }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("円周分割", draft.cylinder.radialDivide, { .minValue = 3,.maxValue = kMaxPrimitiveDivide }); });
		DrawField(anyItemActive, [&]() { return MyGUI::DragInt("高さ分割", draft.cylinder.heightDivide, { .minValue = 1,.maxValue = kMaxPrimitiveDivide }); });
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

	auto it = draft.parameterOverrides.find(var.name);
	if (it != draft.parameterOverrides.end()) {
		return it->second;
	}
	auto defaultIt = cachedMaterial_.parameters.find(var.name);
	return defaultIt != cachedMaterial_.parameters.end() ?
		defaultIt->second : MaterialParameterEditor::DefaultValueForVariable(var);
}

void Engine::PrimitiveRendererInspectorDrawer::DrawReflectedParameters(
	const EditorPanelContext& context, PrimitiveRendererComponent& draft, bool& anyItemActive) {

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, draft.material, EffectiveDefaultMaterial(draft));
	if (!reflection) {
		return;
	}
	const ShaderConstantBufferInfo* cb = FindConstantBuffer(*reflection, MaterialParameterCBuffer::kSurface);
	if (!cb) {
		return;
	}

	ImGui::SeparatorText("シェーダーパラメータ");

	// Drag編集paramを先に出す
	for (const ShaderConstantBufferVariable& var : cb->variables) {

		if (!var.used || MaterialParameterEditor::IsReflectedTextureParam(var)) {
			continue;
		}
		MaterialParameterValue value = ResolveParamValue(draft, var);
		const FloatEditSetting floatSetting{};
		DrawField(anyItemActive, [&]() {

			// valueChangedでプレビュー更新、editFinishedでcommitされUndo/dirtyに乗る
			ValueEditResult result = MaterialParameterEditor::DrawValueEdit(var, value, floatSetting);
			if (result.valueChanged) {
				draft.parameterOverrides[var.name] = value;
			}
			return result;
			});
	}

	// space2のテクスチャSRVをレンダラー個別の上書きとして下にまとめて出す、表示順を整える
	std::vector<const ShaderResourceBinding*> textures;
	for (const ShaderResourceBinding& resource : reflection->resources) {

		if (resource.kind == ShaderBindingKind::SRV && resource.space == 2 &&
			resource.rawType == D3D_SIT_TEXTURE) {
			textures.push_back(&resource);
		}
	}
	std::stable_sort(textures.begin(), textures.end(),
		[](const ShaderResourceBinding* lhs, const ShaderResourceBinding* rhs) {
			return TextureDisplayRank(lhs->name) < TextureDisplayRank(rhs->name);
		});

	for (const ShaderResourceBinding* resource : textures) {

		auto it = draft.parameterOverrides.find(resource->name);
		AssetID textureID{};
		if (it != draft.parameterOverrides.end() && std::holds_alternative<AssetID>(it->second.value)) {
			textureID = std::get<AssetID>(it->second.value);
		}
		DrawField(anyItemActive, [&]() {

			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			auto result = MyGUI::AssetReferenceField(resource->name.c_str(), textureID,
				context.editorContext->assetDatabase, { AssetType::Texture }, setting);
			if (result.valueChanged) {
				MaterialParameterValue value{};
				value.value = textureID;
				draft.parameterOverrides[resource->name] = value;
			}
			return result;
			});
	}
}
