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
	const std::unordered_map<std::string, MaterialParameterValue>& parameters,
	const std::string& name) {

	if (!EnsureMaterialReflection(context, materialID, defaultMaterialID)) {
		return AssetID{};
	}
	return ResolveTextureValue(parameters, name);
}

Engine::MaterialParameterValue Engine::ReflectedMaterialParameterDrawer::ResolveParamValue(
	const std::unordered_map<std::string, MaterialParameterValue>& parameters,
	const ShaderConstantBufferVariable& variable) const {

	const auto overrideIt = parameters.find(variable.name);
	if (overrideIt != parameters.end()) {
		return overrideIt->second;
	}
	const auto defaultIt = cachedMaterial_.parameters.find(variable.name);
	return defaultIt != cachedMaterial_.parameters.end() ?
		defaultIt->second : MaterialParameterEditor::DefaultValueForVariable(variable);
}

Engine::AssetID Engine::ReflectedMaterialParameterDrawer::ResolveTextureValue(
	const std::unordered_map<std::string, MaterialParameterValue>& parameters, const std::string& name) const {

	const auto overrideIt = parameters.find(name);
	if (overrideIt != parameters.end()) {
		if (const AssetID* textureID = std::get_if<AssetID>(&overrideIt->second.value)) {
			if (*textureID) {
				return *textureID;
			}
		}
	}
	const auto defaultIt = cachedMaterial_.parameters.find(name);
	if (defaultIt != cachedMaterial_.parameters.end()) {
		if (const AssetID* textureID = std::get_if<AssetID>(&defaultIt->second.value)) {
			return *textureID;
		}
	}
	return AssetID{};
}
