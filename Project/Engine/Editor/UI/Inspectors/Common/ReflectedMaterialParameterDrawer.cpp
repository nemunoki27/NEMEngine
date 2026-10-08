#include "ReflectedMaterialParameterDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterLookup.h>

// c++
#include <variant>

//============================================================================
//	ReflectedMaterialParameterDrawer classMethods
//============================================================================
const Engine::ShaderReflectionInfo* Engine::ReflectedMaterialParameterDrawer::EnsureMaterialReflection(
	const EditorPanelContext& context, AssetID materialID, AssetID defaultMaterialID) {

	// 既定値の読込を共通の解決処理へ渡す
	return materialReflection_.EnsureReflection(context, materialID, defaultMaterialID);
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

	// IDと用途から対応するTexture入力を探す
	const MaterialParameterID requestedID =
		MaterialParameterID::FromName(name);
	const MaterialParameterSemantic requestedSemantic =
		ResolveMaterialParameterSemantic(name);
	for (const ShaderResourceBinding& resource : reflection->resources) {
		if (!MaterialParameterEditor::IsMaterialTextureResource(resource)) {
			continue;
		}
		const std::string_view displayName =
			MaterialParameterEditor::GetReflectedTextureDisplayName(resource, *reflection);
		const MaterialParameterID parameterID =
			MaterialParameterEditor::GetReflectedTextureParameterID(resource, *reflection);
		const MaterialParameterSemantic semantic =
			MaterialParameterEditor::GetReflectedTextureSemantic(resource, *reflection);
		if (resource.name == name || displayName == name ||
			parameterID == requestedID ||
			(requestedSemantic != MaterialParameterSemantic::None &&
				semantic == requestedSemantic)) {
			return ResolveTextureValue(parameters, parameterID, semantic, displayName);
		}
	}
	return ResolveTextureValue(parameters, requestedID, requestedSemantic, name);
}

Engine::MaterialParameterValue Engine::ReflectedMaterialParameterDrawer::ResolveParamValue(
	const MaterialParameterSet& parameters,
	const ShaderConstantBufferVariable& variable) const {

	// Instanceの値を優先してMaterialの既定値へ戻す
	if (const MaterialParameterValue* value =
		parameters.Find(variable.parameterID)) {

		return *value;
	}
	if (const MaterialParameterValue* value =
		materialReflection_.GetMaterial().parameters.Find(variable.parameterID)) {

		return *value;
	}
	return MaterialParameterEditor::DefaultValueForVariable(variable);
}

Engine::AssetID Engine::ReflectedMaterialParameterDrawer::ResolveTextureValue(
	const MaterialParameterSet& parameters,
	MaterialParameterID parameterID,
	MaterialParameterSemantic semantic,
	std::string_view name) const {

	// 値の有無と空Textureの上書きを区別する
	const MaterialParameterSet& defaults = materialReflection_.GetMaterial().parameters;
	const auto* value = MaterialParameterLookup::Find(parameters, parameterID, semantic, name, &defaults);
	if (!value) value = MaterialParameterLookup::Find(defaults, parameterID, semantic, name);
	const auto* texture = value ? std::get_if<AssetID>(&value->value) : nullptr;
	return texture ? *texture : AssetID{};
}
