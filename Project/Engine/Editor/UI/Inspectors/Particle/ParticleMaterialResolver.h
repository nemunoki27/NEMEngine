#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

// c++
#include <optional>

namespace Engine::ParticleMaterialResolver {

	bool IsParticleMaterialAnimatable(const ShaderConstantBufferVariable& var);
	AssetID ResolvePhaseMaterialID(const ParticleSystemSettings& group, const ParticlePhaseSettings& phase);
	AssetID ResolveTrailMaterialID(const ParticleSystemSettings& group);
	std::optional<MaterialAsset> LoadMaterialAsset(const EditorPanelContext& context, AssetID materialID);
	const ShaderReflectionInfo* FindParticleMaterialReflection(
		const EditorPanelContext& context, const MaterialAsset& material);
	bool ValidateParticleMaterialSelection(const EditorPanelContext& context, AssetID materialID, std::string& outMessage);
	std::vector<ShaderConstantBufferVariable> CollectMaterialParameters(const ShaderReflectionInfo& reflection);
	std::vector<ShaderResourceBinding> CollectMaterialTextures(const ShaderReflectionInfo& reflection);
}
