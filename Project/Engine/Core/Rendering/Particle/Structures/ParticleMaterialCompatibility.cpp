#include "ParticleMaterialCompatibility.h"

//============================================================================
//	ParticleMaterialCompatibility internal
//============================================================================
namespace {

	// 指定名のシェーダーリソースが存在するか確認する
	bool HasResource(const Engine::ShaderReflectionInfo& reflection, const char* name,
		Engine::ShaderBindingKind kind) {

		for (const Engine::ShaderResourceBinding& resource : reflection.resources) {
			if (resource.name == name && resource.kind == kind) {
				return true;
			}
		}
		return false;
	}
}

//============================================================================
//	ParticleMaterialCompatibility functions
//============================================================================
Engine::ParticleMaterialCompatibilityResult Engine::CheckParticleMaterialCompatibility(
	const MaterialAsset& material, const ShaderReflectionInfo* reflection) {

	if (material.usage != MaterialUsage::Generic && material.usage != MaterialUsage::Particle) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"Particle用ではないマテリアルです" };
	}
	if (!reflection) {
		return { ParticleMaterialCompatibilityStatus::PendingReflection,
			"シェーダーreflectionをまだ取得できません" };
	}

	if (!FindConstantBuffer(*reflection, "ViewConstants")) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"ViewConstantsがありません" };
	}
	if (!HasResource(*reflection, "gVertices", ShaderBindingKind::SRV)) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"gVerticesがありません" };
	}
	if (!HasResource(*reflection, "gParticleGeometry", ShaderBindingKind::SRV)) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"gParticleGeometryがありません" };
	}
	if (!HasResource(*reflection, "gParticleMaterials", ShaderBindingKind::SRV)) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"gParticleMaterialsがありません" };
	}

	bool hasBaseColorTexture = false;
	for (const ShaderResourceBinding& resource : reflection->resources) {
		if (resource.name == "baseColorTexture" && resource.kind == ShaderBindingKind::SRV &&
			resource.space == 2 && resource.rawType == D3D_SIT_TEXTURE) {

			hasBaseColorTexture = true;
			break;
		}
	}
	if (!hasBaseColorTexture) {
		return { ParticleMaterialCompatibilityStatus::Incompatible,
			"baseColorTextureがありません" };
	}
	return { ParticleMaterialCompatibilityStatus::Compatible, {} };
}
