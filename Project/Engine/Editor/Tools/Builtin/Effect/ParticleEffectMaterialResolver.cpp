#include "ParticleEffectMaterialResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

// c++
#include <algorithm>
#include <filesystem>

namespace Engine::ParticleEffectMaterialResolver {

	bool IsPaddingName(const std::string& name) {

		return name.find("pad") != std::string::npos || name.find("Pad") != std::string::npos;
	}

	bool IsParticleMaterialAnimatable(const ShaderConstantBufferVariable& var) {

		if (!var.used || var.valueType != D3D_SVT_FLOAT || IsPaddingName(var.name)) {
			return false;
		}
		if (var.name == MaterialParameterNames::BaseColor) {
			return false;
		}
		return true;
	}

	AssetID ResolvePhaseMaterialID(const ParticleEffectAsset& asset,
		const ParticleEffectGroup& group, const ParticleEffectPhase& phase) {

		if (phase.material) {
			return phase.material;
		}
		if (group.material) {
			return group.material;
		}
		return asset.space == PrimitiveRenderSpace::Screen2D ?
			BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
	}

	AssetID ResolveTrailMaterialID(const ParticleEffectAsset& asset, const ParticleEffectGroup& group) {

		if (group.trail.material) {
			return group.trail.material;
		}
		if (group.material) {
			return group.material;
		}
		return asset.space == PrimitiveRenderSpace::Screen2D ?
			BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle;
	}

	std::optional<MaterialAsset> LoadMaterialAsset(const EditorToolContext& context, AssetID materialID) {

		AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
		if (!assetDatabase || !materialID) {
			return std::nullopt;
		}
		const std::filesystem::path materialPath = assetDatabase->ResolveFullPath(materialID);
		if (materialPath.empty()) {
			return std::nullopt;
		}
		MaterialAsset material{};
		const nlohmann::json data = JsonAdapter::Load(materialPath.string(), false);
		if (!FromJson(data, material)) {
			return std::nullopt;
		}
		return material;
	}

	const ShaderReflectionInfo* FindParticleMaterialReflection(
		const EditorToolContext& context, const MaterialAsset& material) {

		if (!context.panelContext || !context.panelContext->renderPipeline) {
			return nullptr;
		}
		return context.panelContext->renderPipeline->FindMaterialDrawReflection(material);
	}

	bool ValidateParticleMaterialSelection(const EditorToolContext& context,
		AssetID materialID, std::string& outMessage) {

		outMessage.clear();
		if (!materialID) {
			return true;
		}
		const std::optional<MaterialAsset> material = LoadMaterialAsset(context, materialID);
		if (!material) {
			outMessage = "マテリアルを読み込めません";
			return false;
		}
		const ParticleMaterialCompatibilityResult compatibility = CheckParticleMaterialCompatibility(
			*material, FindParticleMaterialReflection(context, *material));
		if (compatibility.IsCompatible()) {
			return true;
		}
		if (compatibility.status == ParticleMaterialCompatibilityStatus::PendingReflection &&
			material->usage == MaterialUsage::Particle) {
			return true;
		}
		outMessage = compatibility.message;
		return false;
	}

	std::vector<ShaderConstantBufferVariable> CollectMaterialParameters(const ShaderReflectionInfo& reflection) {

		std::vector<ShaderConstantBufferVariable> variables{};
		if (const ShaderStructuredBufferInfo* buffer =
			FindStructuredBuffer(reflection, "gParticleCustomParameters")) {

			for (const ShaderConstantBufferVariable& var : buffer->variables) {
				if (IsParticleMaterialAnimatable(var)) {
					variables.emplace_back(var);
				}
			}
		}
		std::sort(variables.begin(), variables.end(),
			[](const ShaderConstantBufferVariable& lhs, const ShaderConstantBufferVariable& rhs) {
				return lhs.name < rhs.name;
			});
		return variables;
	}

	std::vector<ShaderResourceBinding> CollectMaterialTextures(const ShaderReflectionInfo& reflection) {

		std::vector<ShaderResourceBinding> textures{};
		for (const ShaderResourceBinding& resource : reflection.resources) {
			if (resource.kind == ShaderBindingKind::SRV && resource.space == 2 &&
				resource.rawType == D3D_SIT_TEXTURE &&
				resource.name != MaterialParameterNames::BaseColorTexture) {
				textures.emplace_back(resource);
			}
		}
		std::sort(textures.begin(), textures.end(),
			[](const ShaderResourceBinding& lhs, const ShaderResourceBinding& rhs) {
				return lhs.name < rhs.name;
			});
		return textures;
	}
}
