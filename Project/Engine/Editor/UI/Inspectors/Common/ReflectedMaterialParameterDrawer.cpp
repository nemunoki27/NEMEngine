#include "ReflectedMaterialParameterDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <filesystem>

//============================================================================
//	ReflectedMaterialParameterDrawer classMethods
//============================================================================
const Engine::ShaderReflectionInfo* Engine::ReflectedMaterialParameterDrawer::EnsureMaterialReflection(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID) {

	if (!context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) {
		return nullptr;
	}
	if (!materialID) {
		materialID = defaultMaterialID;
	}
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

Engine::AssetID Engine::ReflectedMaterialParameterDrawer::ResolveTextureParameter(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID,
	const MaterialParameterSet& parameters,
	const std::string& name) {

	const ShaderReflectionInfo* reflection =
		EnsureMaterialReflection(context, materialID, defaultMaterialID);
	if (!reflection) {
		return AssetID{};
	}

	const MaterialParameterID requestedID =
		MaterialParameterID::FromName(name);
	const MaterialParameterSemantic requestedSemantic =
		ResolveMaterialParameterSemantic(name);
	for (const ShaderResourceBinding& resource : reflection->resources) {
		if (!MaterialParameterEditor::IsMaterialTextureResource(resource)) {
			continue;
		}
		const std::string_view displayName =
			MaterialParameterEditor::GetReflectedTextureDisplayName(
				resource, *reflection);
		const MaterialParameterID parameterID =
			MaterialParameterEditor::GetReflectedTextureParameterID(
				resource, *reflection);
		const MaterialParameterSemantic semantic =
			MaterialParameterEditor::GetReflectedTextureSemantic(
				resource, *reflection);
		if (resource.name == name || displayName == name ||
			parameterID == requestedID ||
			(requestedSemantic != MaterialParameterSemantic::None &&
				semantic == requestedSemantic)) {
			return ResolveTextureValue(
				parameters, parameterID, semantic, displayName);
		}
	}
	return ResolveTextureValue(
		parameters, requestedID, requestedSemantic, name);
}

Engine::MaterialParameterValue Engine::ReflectedMaterialParameterDrawer::ResolveParamValue(
	const MaterialParameterSet& parameters,
	const ShaderConstantBufferVariable& variable) const {

	if (const MaterialParameterValue* value =
		parameters.Find(variable.parameterID)) {

		return *value;
	}
	if (const MaterialParameterValue* value =
		cachedMaterial_.parameters.Find(variable.parameterID)) {

		return *value;
	}
	return MaterialParameterEditor::DefaultValueForVariable(variable);
}

Engine::AssetID Engine::ReflectedMaterialParameterDrawer::ResolveTextureValue(
	const MaterialParameterSet& parameters,
	MaterialParameterID parameterID,
	MaterialParameterSemantic semantic,
	std::string_view name) const {

	const auto findTexture = [parameterID, semantic, name](
		const MaterialParameterSet& source) -> AssetID {

		const MaterialParameterValue* value = nullptr;
		if (parameterID) {
			value = source.Find(parameterID);
		}
		if (!value && semantic != MaterialParameterSemantic::None) {
			value = source.Find(semantic);
		}
		if (!value && !name.empty()) {
			value = source.FindByName(name);
		}
		if (value) {
			if (const AssetID* textureID =
				std::get_if<AssetID>(&value->value)) {
				return *textureID;
			}
		}
		return AssetID{};
		};

	if (const AssetID textureID = findTexture(parameters)) {
		return textureID;
	}
	return findTexture(cachedMaterial_.parameters);
}
