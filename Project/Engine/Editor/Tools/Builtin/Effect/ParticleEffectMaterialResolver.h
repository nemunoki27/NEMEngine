#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <optional>

namespace Engine::ParticleEffectMaterialResolver {

	bool IsParticleMaterialAnimatable(const ShaderConstantBufferVariable& var);
	AssetID ResolvePhaseMaterialID(const ParticleEffectAsset& asset,
		const ParticleEffectGroup& group, const ParticleEffectPhase& phase);
	AssetID ResolveTrailMaterialID(const ParticleEffectAsset& asset, const ParticleEffectGroup& group);
	std::optional<MaterialAsset> LoadMaterialAsset(const EditorToolContext& context, AssetID materialID);
	const ShaderReflectionInfo* FindParticleMaterialReflection(
		const EditorToolContext& context, const MaterialAsset& material);
	bool ValidateParticleMaterialSelection(const EditorToolContext& context, AssetID materialID, std::string& outMessage);
	std::vector<ShaderConstantBufferVariable> CollectMaterialParameters(const ShaderReflectionInfo& reflection);
	std::vector<ShaderResourceBinding> CollectMaterialTextures(const ShaderReflectionInfo& reflection);
}
