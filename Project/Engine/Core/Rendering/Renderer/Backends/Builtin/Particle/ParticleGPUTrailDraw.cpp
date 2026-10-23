#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include "ParticleDrawPreparation.h"
#include <Engine/Core/Rendering/Particle/ParticleGPUTrailTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

using namespace Engine;

void ParticleRenderBackend::DrawGPUTrails(const RenderDrawContext& context, const RenderItem& item,
	const ParticleEmitterRuntime& emitter, const ParticleGPUDrawData& data, const ParticleGPUViewConstants& constants) {

	auto& graphics = *context.graphicsCore;
	auto& platform = graphics.GetDXObject();
	const auto& trail = emitter.renderSettings.trail;
	const auto materialID = trail.material ? trail.material : emitter.renderSettings.material;
	BackendDrawCommon::ResolvedMaterialPass material;
	if (!ParticleDrawPreparation::ResolveGPUParticlePass(context, materialID, constants.is2D != 0, material)) {
		emitter.gpu->Reject("GPU Trailは標準Particle Shaderのマテリアルに対応しています");
		return;
	}
	const auto pipelineID = constants.is2D ? BuiltinAssets::Pipelines::ParticleGPUTrailDraw2D : BuiltinAssets::Pipelines::ParticleGPUTrailDraw;
	const auto* pipeline = context.pipelineCache->GetORCreate(platform, *context.assetLibrary, pipelineID,
		PipelineVariantKind::GraphicsVertex, context.GetRTVFormats(), context.dsvFormat, context.runtimeFeatures);
	if (!pipeline) return;
	const auto viewAllocation = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
	const auto trailConstants = MakeParticleGPUTrailConstants(trail, emitter.gpu->definition->settings->emitter.maxParticles);
	const auto trailAllocation = constantBufferAllocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), trailConstants);
	auto* commands = BackendDrawCommon::SetupGraphicsPipeline(context, *pipeline, item.blendMode);
	SyncAndBindRegistry(*pipeline, context, commands);
	RootBindingCommand::SetGraphicsCBV(commands, pipeline->FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), viewAllocation.gpuAddress);
	RootBindingCommand::SetGraphicsCBV(commands, pipeline->FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), trailAllocation.gpuAddress);
	const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
		RootBindingCommand::SetGraphicsSRV(commands, pipeline->FindBindingByName(name, ShaderBindingKind::SRV), address);
	};
	srv("gGPUTrails", data.trails);
	srv("gGPUTrailPoints", data.trailPoints);
	srv("gGPURibbonOrder", data.ribbonOrder);
	srv("gGPUTrailSegments", data.trailSegments);
	MaterialParameterSet overrides;
	ParticleDrawPreparation::BuildPhaseMaterialInstance(trail.materialSettings, overrides);
	BindMaterial(context, *pipeline, *material.material, overrides.empty() ? nullptr : &overrides, commands);
	// 粒子本体とは別の寿命と描画個数を使う
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	gpuService_.DrawTrails(graphics, data);
}
