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
			std::unordered_map<std::string, MaterialParameterValue>& parameters, DrawFieldFn&& drawField);
		// テクスチャの実効値を取得する
		AssetID ResolveTextureParameter(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID,
			const std::unordered_map<std::string, MaterialParameterValue>& parameters,
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
			const std::unordered_map<std::string, MaterialParameterValue>& parameters,
			const ShaderConstantBufferVariable& variable) const;
		// テクスチャ最終値を解決する
		AssetID ResolveTextureValue(
			const std::unordered_map<std::string, MaterialParameterValue>& parameters,
			const std::string& name) const;
	};
} // Engine

//============================================================================
//	ReflectedMaterialParameterDrawer classTemplateMethods
//============================================================================
template <typename DrawFieldFn>
inline void Engine::ReflectedMaterialParameterDrawer::Draw(const EditorPanelContext& context,
	AssetID materialID, AssetID defaultMaterialID,
	std::unordered_map<std::string, MaterialParameterValue>& parameters, DrawFieldFn&& drawField) {

	ImGui::SeparatorText("シェーダーパラメータ");

	const ShaderReflectionInfo* reflection = EnsureMaterialReflection(context, materialID, defaultMaterialID);
	if (!reflection) {
		ImGui::TextDisabled("マテリアルのパラメータを取得できません");
		return;
	}

	bool hasParameter = false;
	MaterialParameterLayout layout{};
	layout.Build(*reflection, MaterialParameterCBuffer::kSurface);
	if (layout.IsValid()) {

		for (const ShaderConstantBufferVariable& variable : layout.GetVariables()) {

			if (!variable.used) {
				continue;
			}
			hasParameter = true;
			MaterialParameterValue value = ResolveParamValue(parameters, variable);
			drawField([&]() {

				ValueEditResult result = MaterialParameterEditor::DrawValueEdit(variable, value);
				if (result.valueChanged) {
					parameters[variable.name] = value;
				}
				return result;
				});
		}
	}

	std::vector<const ShaderResourceBinding*> textures;
	for (const ShaderResourceBinding& resource : reflection->resources) {

		if (resource.kind == ShaderBindingKind::SRV && resource.space == 2 &&
			resource.rawType == D3D_SIT_TEXTURE) {
			textures.emplace_back(&resource);
		}
	}
	std::stable_sort(textures.begin(), textures.end(),
		[](const ShaderResourceBinding* lhs, const ShaderResourceBinding* rhs) {
			return lhs->bindPoint < rhs->bindPoint;
		});

	for (const ShaderResourceBinding* resource : textures) {

		hasParameter = true;
		AssetID textureID = ResolveTextureValue(parameters, resource->name);
		drawField([&]() {

			AssetEditSetting setting{};
			setting.graphicsCore = context.graphicsCore;
			ValueEditResult result = MyGUI::AssetReferenceField(resource->name.c_str(), textureID,
				context.editorContext->assetDatabase, { AssetType::Texture }, setting);
			if (result.valueChanged) {
				parameters[resource->name].value = textureID;
			}
			return result;
			});
	}

	if (!hasParameter) {
		ImGui::TextDisabled("編集可能なシェーダーパラメータがありません");
	}
}
