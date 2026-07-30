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

	if (!EnsureMaterialReflection(context, materialID, defaultMaterialID)) {
		return AssetID{};
	}
	return ResolveTextureValue(parameters, name);
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
	const MaterialParameterSet& parameters, const std::string& name) const {

	const MaterialParameterID parameterID =
		MaterialParameterID::FromName(name);
	if (const MaterialParameterValue* value =
		parameters.Find(parameterID)) {

		if (const AssetID* textureID = std::get_if<AssetID>(&value->value)) {
			if (*textureID) {
				return *textureID;
			}
		}
	}
	if (const MaterialParameterValue* value =
		cachedMaterial_.parameters.Find(parameterID)) {

		if (const AssetID* textureID = std::get_if<AssetID>(&value->value)) {
			return *textureID;
		}
	}
	return AssetID{};
}
