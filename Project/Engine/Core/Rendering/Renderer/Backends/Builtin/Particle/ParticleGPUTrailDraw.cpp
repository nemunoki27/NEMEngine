#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "ParticleDrawPreparation.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUTrailTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <exception>

using namespace Engine;

void ParticleRenderBackend::DrawGPUTrails(const RenderDrawContext& context, const RenderItem& item,
	const ParticleEmitterRuntime& emitter, const ParticleGPUDrawData& data, const ParticleGPUViewConstants& constants) {

	auto& graphics = *context.graphicsCore;
	auto& platform = graphics.GetDXObject();
	const auto& trail = emitter.renderSettings.trail;
	const auto materialID = trail.material ? trail.material : emitter.renderSettings.material;
	BackendDrawCommon::ResolvedMaterialPass material;
	if (!ParticleDrawPreparation::ResolveGPUParticlePass(context, materialID, constants.is2D != 0, material)) {
		emitter.gpu->Reject("GPU Trailは標準ParticleまたはParticle ShaderGraphのマテリアルに対応しています");
		return;
	}
	const auto pipelineID = constants.is2D ? BuiltinAssets::Pipelines::ParticleGPUTrailDraw2D : BuiltinAssets::Pipelines::ParticleGPUTrailDraw;
	const PipelineState* pipeline = nullptr;
	if (material.material->shaderGraph) {
		const auto shaderID = ShaderGraphArtifactCache::MakeParticleGPUShaderID(material.material->shaderGraph);
		pipeline = context.pipelineCache->GetORCreateComposed(platform, *context.assetLibrary, material.pass->pipeline,
			pipelineID, shaderID, PipelineVariantKind::GraphicsVertex, context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures);
	} else {
		pipeline = context.pipelineCache->GetORCreate(platform, *context.assetLibrary, pipelineID,
			PipelineVariantKind::GraphicsVertex, context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures);
	}
	if (!pipeline) return;
	const auto viewAllocation = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
	const auto trailConstants = MakeParticleGPUTrailConstants(trail, emitter.gpu->definition->settings->emitter.maxParticles);
	const auto trailAllocation = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), trailConstants);
	auto* commands = BackendDrawCommon::SetupGraphicsPipeline(context, *pipeline, item.blendMode);
	SyncAndBindRegistry(*pipeline, context, commands);
	RootBindingCommand::SetGraphicsCBV(commands, pipeline->FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), viewAllocation.gpuAddress);
	RootBindingCommand::SetGraphicsCBV(commands, pipeline->FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), trailAllocation.gpuAddress);
	const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
		if (const auto* binding = pipeline->FindBindingByName(name, ShaderBindingKind::SRV))
			RootBindingCommand::SetGraphicsSRV(commands, binding, address);
	};
	srv("gGPUTrails", data.trails);
	srv("gGPUTrailPoints", data.trailPoints);
	srv("gGPURibbonOrder", data.ribbonOrder);
	srv("gGPUTrailSegments", data.trailSegments);
	MaterialParameterSet overrides;
	ParticleDrawPreparation::BuildPhaseMaterialInstance(trail.materialSettings, overrides);
	BindMaterial(context, *pipeline, *material.material, overrides.empty() ? nullptr : &overrides, commands);
	BindGPUSurface(context, item, emitter.renderSettings, *pipeline, false,
		ParticleDrawPreparation::ResolveNormalTexture(emitter.renderSettings, trail.materialSettings, *material.material), commands);
	try {
		BindGPUMaterialCurves(context, emitter.renderSettings, material, *pipeline, UINT32_MAX,
			trail.materialSettings, ParticleCustomDataSettings{}, commands);
	} catch (const std::exception& error) {
		emitter.gpu->Reject(error.what());
		return;
	}
	// 粒子本体とは別の寿命と描画個数を使う
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gpuService_.DrawTrails(graphics, data);
}
