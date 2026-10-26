#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/ParticleEnvironmentConstants.h>

// c++
#include <array>
#include <span>

using namespace Engine;

namespace {

	struct ParticleSurfaceConstants {

		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
		Matrix4x4 cameraView = Matrix4x4::Identity();
		Vector4 cameraPosition{};
		Vector4 cameraForward{};
		uint32_t flags = 0;
		uint32_t depthAvailable = 0;
		uint32_t normalAvailable = 0;
		uint32_t customDataAvailable = 0;
		Vector4 parameters{};
	};
	static_assert(sizeof(ParticleSurfaceConstants) == 192);

	struct ParticleShadowFallbackConstants {

		std::array<Matrix4x4, 4> viewProjections{};
		Vector4 cascadeSplits{};
		uint32_t available = 0;
		uint32_t lightIndex = UINT32_MAX;
		float depthBias = 0.00002f;
		float reserved = 0;
	};
}

void ParticleRenderBackend::BindGPUSurface(const RenderDrawContext& context, const RenderItem& item,
	const ParticleRenderSettings& settings, const PipelineState& pipeline, bool customDataAvailable, AssetID normalTexture,
	ID3D12GraphicsCommandList* commands) {

	const auto* surfaceBinding = pipeline.FindBindingByName("ParticleSurfaceConstants", ShaderBindingKind::CBV);
	if (!surfaceBinding) return;
	auto& platform = context.graphicsCore->GetDXObject();
	const auto& surface = settings.surface;
	const auto* camera = context.view->FindCamera(item.cameraDomain);
	ParticleSurfaceConstants constants;
	constants.inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
	constants.cameraView = camera->matrices.viewMatrix;
	constants.cameraPosition = Vector4(camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z, 0);
	constants.cameraForward = Vector4(camera->forward.x, camera->forward.y, camera->forward.z, 0);
	const bool is3D = settings.space == PrimitiveRenderSpace::World3D;
	constants.flags = (surface.lit && is3D ? 1u : 0u) | (surface.receiveShadows ? 2u : 0u) |
		(surface.receiveIBL ? 4u : 0u) | (surface.softParticles && is3D ? 8u : 0u);
	const auto* depth = context.bufferRegistry->Find("gParticleSceneDepth");
	constants.depthAvailable = depth && depth->srvGPUHandle.ptr ? 1u : 0u;
	constants.normalAvailable = normalTexture ? 1u : 0u;
	constants.customDataAvailable = customDataAvailable ? 1u : 0u;
	constants.parameters = Vector4(surface.softDistance, surface.normalScale, surface.metallic, surface.roughness);
	const auto allocation = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
	RootBindingCommand::SetGraphicsCBV(commands, surfaceBinding, allocation.gpuAddress);
	const auto white = context.graphicsCore->GetBuiltinTextureLibrary().GetWhiteTexture()->gpuHandle;
	const auto bindTexture = [&](std::string_view name, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV))
			RootBindingCommand::SetGraphicsSRV(commands, binding, 0, handle);
	};
	if (!constants.depthAvailable) bindTexture("gParticleSceneDepth", white);
	if (normalTexture) {
		const auto* texture = BackendDrawCommon::ResolveTextureAsset(context, *context.graphicsCore, normalTexture,
			TextureColorSpace::Linear);
		bindTexture("particleNormalTexture", texture ? texture->gpuHandle : white);
	} else {
		bindTexture("particleNormalTexture", white);
	}
	// 影を生成しない補助Viewでも有効な定数とTextureを置く
	if (!context.bufferRegistry->Find("DirectionalShadowConstants")) {
		if (const auto* binding = pipeline.FindBindingByName("DirectionalShadowConstants", ShaderBindingKind::CBV)) {
			const auto fallback = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(),
				ParticleShadowFallbackConstants{});
			RootBindingCommand::SetGraphicsCBV(commands, binding, fallback.gpuAddress);
		}
	}
	if (!context.bufferRegistry->Find("ParticleEnvironmentConstants")) {
		if (const auto* binding = pipeline.FindBindingByName("ParticleEnvironmentConstants", ShaderBindingKind::CBV)) {
			const auto fallback = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(),
				ParticleEnvironmentConstants{});
			RootBindingCommand::SetGraphicsCBV(commands, binding, fallback.gpuAddress);
		}
	}
	for (uint32_t cascade = 0; cascade < 4; ++cascade) {
		const auto name = "gDirectionalShadowMap" + std::to_string(cascade);
		if (!context.bufferRegistry->Find(name)) bindTexture(name, white);
	}
}

void ParticleRenderBackend::BindGPUMaterialCurves(const RenderDrawContext& context, const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& material, const PipelineState& pipeline, uint32_t phase,
	const ParticlePhaseMaterialSettings& phaseMaterial, const ParticleCustomDataSettings& customData,
	ID3D12GraphicsCommandList* commands) {

	if (!material.material->shaderGraph) return;
	if (!pipeline.FindBindingByName("gParticleCustomParameters", ShaderBindingKind::SRV) &&
		!pipeline.FindBindingByName("gParticleMaterialCurveHeaders", ShaderBindingKind::SRV) &&
		!pipeline.FindBindingByName("gParticleMaterialCurveSamples", ShaderBindingKind::SRV)) return;
	auto& platform = context.graphicsCore->GetDXObject();
	const auto layout = BuildParticleCustomParameterLayout(pipeline.GetGraphicsReflection(), &material.material->parameters,
		[&](MaterialParameterSemantic semantic, const AssetID& id) {
			return BackendDrawCommon::ResolveMaterialTextureIndex(context, semantic, id);
		});
	const uint64_t revision = context.pipelineCache->GetContentRevision() ^ (*context.assetLibrary->GetMaterialRevision() << 32);
	const auto& curves = materialCurveCache_.Get(settings, material.materialID, phase, revision, layout, phaseMaterial, customData);
	const auto upload = [&](const char* name, const auto& values) {
		const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV);
		if (!binding || values.empty()) return;
		const auto bytes = std::as_bytes(std::span(values));
		const auto allocation = materialCurveUpload_.AllocateAndUploadBytes(platform.GetResourceRetirement(), platform.GetDevice(),
			std::span(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
		RootBindingCommand::SetGraphicsSRV(commands, binding, allocation.gpuAddress);
	};
	upload("gParticleCustomParameters", layout.defaultData);
	upload("gParticleMaterialCurveHeaders", curves.headers);
	upload("gParticleMaterialCurveSamples", curves.samples);
}
