#include "MaterialParameterLookup.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <algorithm>

const Engine::MaterialParameterValue* Engine::MaterialParameterLookup::Find(const MaterialParameterSet& parameters,
	MaterialParameterID id, MaterialParameterSemantic semantic, std::string_view name, const MaterialParameterSet* defaults) {

	if (const MaterialParameterValue* value = parameters.Find(id)) {
		return value;
	}
	if (semantic != MaterialParameterSemantic::None) {
		if (const MaterialParameterValue* value = parameters.Find(semantic)) {
			return value;
		}
	}
	if (const MaterialParameterValue* value = parameters.FindByName(name)) {
		return value;
	}
	if (defaults) {
		for (const MaterialParameterRecord& record : defaults->GetRecords()) {
			if (record.id == id) {
				return parameters.FindByName(record.namedValue.first);
			}
		}
	}
	return nullptr;
}

bool Engine::MaterialParameterLookup::IsTextureSemantic(MaterialParameterSemantic semantic) {

	switch (semantic) {
	case MaterialParameterSemantic::BaseColorTexture:
	case MaterialParameterSemantic::NormalTexture:
	case MaterialParameterSemantic::MetallicRoughnessTexture:
	case MaterialParameterSemantic::RoughnessTexture:
	case MaterialParameterSemantic::MetallicTexture:
	case MaterialParameterSemantic::EmissiveTexture:
	case MaterialParameterSemantic::AmbientOcclusionTexture:
	case MaterialParameterSemantic::DisplacementTexture:
	case MaterialParameterSemantic::OpacityTexture:
		return true;
	default:
		return false;
	}
}

bool Engine::MaterialParameterLookup::IsTextureResource(const ShaderResourceBinding& resource) {

	return resource.kind == ShaderBindingKind::SRV && resource.space == 2 && resource.rawType == D3D_SIT_TEXTURE;
}

bool Engine::MaterialParameterLookup::IsSameParameter(const ShaderConstantBufferVariable& variable,
	const ShaderResourceBinding& resource) {

	if (variable.parameterID && resource.parameterID) return variable.parameterID == resource.parameterID;
	if (variable.semantic != MaterialParameterSemantic::None && resource.semantic != MaterialParameterSemantic::None) {
		return variable.semantic == resource.semantic;
	}
	return variable.name == resource.name;
}

bool Engine::MaterialParameterLookup::IsTexture(const ShaderConstantBufferVariable& variable, const ShaderReflectionInfo& reflection) {

	if (variable.valueType != D3D_SVT_UINT) return false;
	// 手書きShaderの既知Textureも明示Metadataと同じ扱いにする
	if (variable.isTexture || IsTextureSemantic(variable.semantic) ||
		IsTextureSemantic(ResolveMaterialParameterSemantic(variable.name)) ||
		variable.parameterID == MaterialParameterIDs::SpecularTexture || variable.name == MaterialParameterNames::SpecularTexture) return true;
	return std::any_of(reflection.resources.begin(), reflection.resources.end(), [&](const auto& resource) {
		return IsTextureResource(resource) && IsSameParameter(variable, resource);
	});
}


bool Engine::MaterialParameterLookup::ReferencesAsset(const MaterialParameterSet& parameters, AssetID assetID) {

	if (!assetID) {
		return false;
	}
	for (const auto& [name, parameter] : parameters) {

		const AssetID* reference = std::get_if<AssetID>(&parameter.value);
		if (reference && *reference == assetID) {
			return true;
		}
	}
	return false;
}
