#include "MaterialAsset.h"

//============================================================================
//	DecalMaterialAsset functions
//============================================================================
Engine::MaterialAsset Engine::CreateDefaultDecalMaterialAsset(std::string_view name) {

	MaterialAsset material;
	material.name = name.empty() ? "NewDecalMaterial" : std::string(name);
	material.domain = MaterialDomain::Decal;
	material.usage = MaterialUsage::Decal;
	// 表面を不用意に変更しないよう色だけを有効にする
	material.parameters.Set("affectColor", MaterialParameterValue{.value = true});
	material.parameters.Set("affectNormal", MaterialParameterValue{.value = false});
	material.parameters.Set("affectSurface", MaterialParameterValue{.value = false});
	material.parameters.Set("affectEmission", MaterialParameterValue{.value = false});
	material.parameters.Set("color", MaterialParameterValue{.value = Color4::White()});
	material.parameters.Set("opacity", MaterialParameterValue{.value = 1.0f});
	material.parameters.Set("normalStrength", MaterialParameterValue{.value = 1.0f});
	material.parameters.Set("metallic", MaterialParameterValue{.value = 0.0f});
	material.parameters.Set("roughness", MaterialParameterValue{.value = 0.5f});
	material.parameters.Set("emissiveColor", MaterialParameterValue{.value = Color4::White()});
	material.parameters.Set("emissiveIntensity", MaterialParameterValue{.value = 0.0f});
	for (const auto* texture : {"baseColorTexture", "normalTexture", "metallicRoughnessTexture", "emissiveTexture"}) {
		material.parameters.Set(texture, MaterialParameterValue{.value = AssetID{}});
	}
	return material;
}
