#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Common/MaterialParameterEditor.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <utility>
#include <variant>
#include <vector>

namespace Engine {

	//============================================================================
	//	ReflectedMaterialParameterDrawer class
	//	サーフェスマテリアルのreflection駆動パラメータ編集
	//============================================================================
	class ReflectedMaterialParameterDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ReflectedMaterialParameterDrawer() = default;
		~ReflectedMaterialParameterDrawer() = default;

		// reflectionした定数とテクスチャを描画する
		template <typename DrawFieldFn>
		void Draw(const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID,
			MaterialParameterSet& parameters, DrawFieldFn&& drawField);
		// テクスチャの実効値を取得する
		AssetID ResolveTextureParameter(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID,
			const MaterialParameterSet& parameters,
			const std::string& name);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// マテリアル既定値とreflection解決のためのキャッシュ
		AssetID cachedMaterialID_{};
		MaterialAsset cachedMaterial_{};
		bool cachedMaterialValid_ = false;

		//--------- functions ----------------------------------------------------

		// マテリアルのDrawパスreflectionを解決する
		const ShaderReflectionInfo* EnsureMaterialReflection(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID);
		// param最終値を解決する
		MaterialParameterValue ResolveParamValue(
			const MaterialParameterSet& parameters,
			const ShaderConstantBufferVariable& variable) const;
		// テクスチャ最終値を解決する
		AssetID ResolveTextureValue(
			const MaterialParameterSet& parameters,
			MaterialParameterID parameterID,
			MaterialParameterSemantic semantic,
			std::string_view name) const;
	};
} // Engine

//============================================================================
//	ReflectedMaterialParameterDrawer classTemplateMethods
//============================================================================
template <typename DrawFieldFn>
inline void Engine::ReflectedMaterialParameterDrawer::Draw(const EditorPanelContext& context,
	AssetID materialID, AssetID defaultMaterialID,
	MaterialParameterSet& parameters, DrawFieldFn&& drawField) {

	ImGui::SeparatorText("シェーダーパラメータ");

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, materialID, defaultMaterialID);
	if (!reflection) {
		ImGui::TextDisabled("マテリアルのパラメータを取得できません");
		return;
	}

	bool hasParameter = false;
	MaterialParameterLayout layout{};
	layout.Build(*reflection, MaterialParameterCBuffer::kSurface);
	std::vector<const ShaderConstantBufferVariable*> scalarVariables;
	std::vector<const ShaderConstantBufferVariable*> textureVariables;
	if (layout.IsValid()) {

		for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {

			if (!variable.used ||
				MaterialParameterEditor::IsInternalPaddingParameter(variable)) {
				continue;
			}
			if (MaterialParameterEditor::IsReflectedTextureParam(
				variable, *reflection)) {
				textureVariables.emplace_back(&variable);
			} else {
				scalarVariables.emplace_back(&variable);
			}
		}
	}
	MaterialParameterEditor::SortScalarParametersForDisplay(scalarVariables);
	MaterialParameterEditor::SortTextureParametersForDisplay(textureVariables);

	for (const ShaderConstantBufferVariable* variable : scalarVariables) {
		hasParameter = true;
		MaterialParameterValue value = ResolveParamValue(parameters, *variable);
		drawField([&]() {

			ValueEditResult result = MaterialParameterEditor::DrawValueEdit(
				*variable, value);
			if (result.valueChanged) {
				parameters.Set(
					variable->parameterID, variable->name,
					variable->semantic, value);
			}
			return result;
			});
	}

	const auto drawTexture = [&](MaterialParameterID parameterID,
		MaterialParameterSemantic semantic, std::string_view displayName) {

		hasParameter = true;
		AssetID textureID = ResolveTextureValue(
			parameters, parameterID, semantic, displayName);
		drawField([&]() {

			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			ValueEditResult result = MyGUI::AssetReferenceField(
				displayName.data(), textureID,
				context.editorContext->assetDatabase,
				{ AssetType::Texture }, setting);
			if (result.valueChanged) {
				MaterialParameterValue value{};
				value.value = textureID;
				parameters.Set(
					parameterID, displayName, semantic, value);
			}
			return result;
			});
		};

	for (const ShaderConstantBufferVariable* variable : textureVariables) {
		drawTexture(variable->parameterID,
			variable->semantic, variable->name);
	}

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
		textures.emplace_back(&resource);
	}
	std::stable_sort(textures.begin(), textures.end(),
		[](const ShaderResourceBinding* lhs, const ShaderResourceBinding* rhs) {
			return MaterialParameterEditor::GetTextureDisplayRank(
				lhs->semantic, lhs->name) <
				MaterialParameterEditor::GetTextureDisplayRank(
					rhs->semantic, rhs->name);
		});

	for (const ShaderResourceBinding* resource : textures) {

		const std::string_view displayName =
			MaterialParameterEditor::GetReflectedTextureDisplayName(
				*resource, *reflection);
		const MaterialParameterID parameterID =
			MaterialParameterEditor::GetReflectedTextureParameterID(
				*resource, *reflection);
		const MaterialParameterSemantic semantic =
			MaterialParameterEditor::GetReflectedTextureSemantic(
				*resource, *reflection);
		drawTexture(parameterID, semantic, displayName);
	}

	if (!hasParameter) {
		ImGui::TextDisabled("編集可能なシェーダーパラメータがありません");
	}
}
