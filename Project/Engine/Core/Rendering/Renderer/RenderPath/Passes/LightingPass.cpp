#include "LightingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderRequirements.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

	constexpr uint32_t kShadowCascadeCount = Engine::DirectionalShadowMapState::kCascadeCount;
}

//============================================================================
//	LightingPass classMethods
//============================================================================

Engine::LightingPass::LightingPass(const RenderPipelineDeps& deps) : deps_(deps) {

	// GBufferの各アタッチメントをregister順でスロット登録する、t0-t5はSceneMainのcolor並びと一致
	albedoSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
	normalSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 1, 0);
	worldPosSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 2, 0);
	materialSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 3, 0);
	emissiveSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 4, 0);
	flagsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 5, 0);
	ambientOcclusionSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 17, 0);
	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 0);
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
		shadowMapSlots_[cascade] = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 13 + cascade, 0);
	}
}

void Engine::LightingPass::EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat) {

	constantBuffers_.Connect(graphicsCore.GetDXObject().GetResourceRetirement());

	if (initialized_) {
		return;
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	// 画面全体を覆う共通VSを使い回す
	desc.preRaster.file = "Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl";
	desc.preRaster.shader = BuiltinAssets::Shaders::FullscreenCopy;
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	// 通常LightingはDescriptor Table経由でSM6.0へ対応する
	desc.pixel.file = "Builtin/Lighting/deferredLighting.PS.hlsl";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLighting;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";

	// cubemap用の静的サンプラー
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	desc.staticSamplers.push_back(sampler);

	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

	// 全画面合成なので深度は使わない
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = {1, 0};
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = colorFormat;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;

	// シャドウ無し版、gSceneTLASを参照しない
	desc.pixel.entry = "main";
	initialized_ = (pipeline_ = PipelineStateBuilder::CreateGraphics(
						graphicsCore.GetDXObject().GetResourceRetirement(), device, compiler, desc)) != nullptr;

	// TLASシャドウ付き版、inlineRT非対応環境ではPSO構築に失敗するためフラグで持つ
	desc.pixel.entry = "mainShadowed";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLightingShadowed;
	desc.pixel.profile = "ps_6_6";
	// 非対応GPUではInline Ray Tracing版を生成しない
	if (graphicsCore.GetDXObject().GetFeatureController().GetSupport().SupportsInlineRayTracingPath()) {
		shadowedAvailable_ = (pipelineShadowed_ = PipelineStateBuilder::CreateGraphics(
			graphicsCore.GetDXObject().GetResourceRetirement(), device, compiler, desc)) != nullptr;
	}
}

Engine::DxConstBuffer<Engine::LightingPass::LightingConstants>& Engine::LightingPass::AllocateConstantBuffer(
	GraphicsCore& graphicsCore) {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (constantBufferFrameSerials_[frameIndex] != frameSerial) {
		constantBufferFrameSerials_[frameIndex] = frameSerial;
		constantBufferIndices_[frameIndex] = 0;
	}
	auto& buffers = constantBuffers_[frameIndex];
	uint32_t& bufferIndex = constantBufferIndices_[frameIndex];
	if (buffers.size() <= bufferIndex) {

		auto buffer = std::make_unique<DxConstBuffer<LightingConstants>>();
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(), graphicsCore.GetDXObject().GetDevice());
		buffers.push_back(std::move(buffer));
	}
	return *buffers[bufferIndex++];
}

void Engine::LightingPass::BindGBufferSRV(
	ID3D12GraphicsCommandList* commandList, PipelineBindingCache::SlotID slot, RenderTexture2D* texture) {

	if (!bindCache_.Has(slot) || !texture) {
		return;
	}
	RootBindingCommand::SetGraphicsSRV(commandList, bindCache_.Get(slot), 0, texture->GetSRVGPUHandle());
}

void Engine::LightingPass::RetainViews(const std::unordered_set<std::string>& viewKeys) {

	inlineShadows_.RetainViews(viewKeys);
	directionalShadows_.RetainViews(viewKeys);
	std::erase_if(shadowRequirements_, [&](const auto& entry) { return !viewKeys.contains(entry.first); });
}

void Engine::LightingPass::Execute(
	GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.view) {
		return;
	}

	// GBufferと合成先が揃っていなければ描けない
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneMain || !sceneFinal) {
		return;
	}
	RenderTexture2D* destColor = sceneFinal->GetColorTexture(0);
	if (!destColor) {
		return;
	}

	auto& profiler = GPUFrameProfiler::GetInstance();
	const std::string profilePrefix = std::string(EnumAdapter<RenderViewKind>::ToStringView(context.kind)) + "/Lighting";
	EnsurePipeline(graphicsCore, destColor->GetFormat());
	if (!initialized_) {
		return;
	}
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const bool tlasAvailable = context.bufferRegistry.Find("gSceneTLAS") != nullptr;
	const bool requestShadow =
		context.hasShadowCastingLight && shadowedAvailable_ && runtimeFeatures.useInlineRayTracing && tlasAvailable &&
		!context.disableInlineRayTracing;
	// 散乱と粒子の受光に必要なときだけ追加のCascadeを用意する
	const bool volumetricShadows = RequiresDirectionalShadowMap(graphicsCore, passBuckets, context);
	const bool useShadow = requestShadow && inlineShadows_.Render(graphicsCore, context, deps_);
	profiler.BeginPass(commandList, profilePrefix + " ShadowMap");
	const bool shadowMapAvailable = (!useShadow || volumetricShadows) &&
		directionalShadows_.Render(graphicsCore, passBuckets, context, deps_);
	profiler.EndPass(commandList);
	profiler.BeginPass(commandList, profilePrefix + " Preparation");

	// GBufferをシェーダーリードへ、SceneFinalをレンダーターゲットへ遷移してバインド
	sceneMain->TransitionForShaderRead(*dxCommand);
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);

	dxCommand->SetDescriptorHeaps({graphicsCore.GetSRVDescriptor().GetDescriptorHeap()});

	// 背景に使うskyboxを探す、最初に見つかったskyboxが対象
	const SceneSkyboxInfo skybox = SceneSkyboxResolver::Resolve(
		graphicsCore, context.assetDatabase, context.world, context.view->GetCullingMask(RenderCameraDomain::Perspective));
	profiler.EndPass(commandList);
	profiler.BeginPass(commandList, profilePrefix + " Irradiance");
	uint32_t irradianceCubemapIndex = 0xFFFFFFFF;
	if (skybox.found) {

		// 拡散IBL用の放射照度cubemapを更新する、描画PSO設定前にコンピュートを積む
		if (skybox.physicalEnvironmentEnabled) {
			irradianceCubemapIndex = skybox.irradianceIndex;
		} else {
			irradianceMap_.Update(graphicsCore, skybox.cubemapAssetID, skybox.cubemapIndex);
			irradianceCubemapIndex = irradianceMap_.GetSRVIndex();
		}
	}
	profiler.EndPass(commandList);

	// 生成に成功した影に合わせてLightingを設定
	profiler.BeginPass(commandList, profilePrefix);
	PipelineState& activePipeline = useShadow ? *pipelineShadowed_ : *pipeline_;

	commandList->SetGraphicsRootSignature(activePipeline.GetRootSignature());
	commandList->SetPipelineState(activePipeline.GetGraphicsPipeline(BlendMode::Normal));
	activePipeline.BindGlobalDescriptorTablesGraphics(commandList, graphicsCore.GetSRVDescriptor().GetGPUHandle(0));

	// ライトバッファとTLASを名前でバインド
	registryAutoBindTable_.Sync(activePipeline, context.bufferRegistry);
	registryAutoBindTable_.BindGraphics(context.bufferRegistry, commandList);

	// GBufferの各SRVをバインドする
	bindCache_.Sync(activePipeline);
	BindGBufferSRV(commandList, albedoSlot_, sceneMain->GetColorTexture(0));
	BindGBufferSRV(commandList, normalSlot_, sceneMain->GetColorTexture(1));
	BindGBufferSRV(commandList, worldPosSlot_, sceneMain->GetColorTexture(2));
	BindGBufferSRV(commandList, materialSlot_, sceneMain->GetColorTexture(3));
	BindGBufferSRV(commandList, emissiveSlot_, sceneMain->GetColorTexture(4));
	BindGBufferSRV(commandList, flagsSlot_, sceneMain->GetColorTexture(5));
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {

		DepthTexture2D* shadowDepth = directionalShadows_.GetDepthTexture(cascade);
		if (!shadowDepth || !bindCache_.Has(shadowMapSlots_[cascade])) {
			continue;
		}
		shadowDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		RootBindingCommand::SetGraphicsSRV(
			commandList, bindCache_.Get(shadowMapSlots_[cascade]), 0, shadowDepth->GetSRVGPUHandle());
	}

	// 背景復元用の逆ビュー射影と視点を集める、2Dビューでは背景を出さない
	LightingConstants constants{};
	constants.ambientIntensity = 0.03f;
	constants.skyboxColor = skybox.found ? skybox.color : graphicsCore.GetContext().GetGraphicsSetting().sceneClearColor;
	constants.skyboxCubemapIndex = skybox.found ? skybox.cubemapIndex : 0xFFFFFFFF;
	constants.hasSkybox = skybox.found ? 1u : 0u;
	constants.irradianceCubemapIndex = irradianceCubemapIndex;
	constants.iblIntensity = skybox.iblIntensity;
	constants.physicalEnvironmentEnabled = skybox.physicalEnvironmentEnabled ? 1u : 0u;
	constants.environmentSpecularIndex = skybox.specularIndex;
	constants.environmentBRDFIndex = skybox.brdfIndex;
	constants.softShadowSampleCount = runtimeFeatures.softShadowSampleCount;
	constants.filteredShadowAvailable = useShadow ? 1u : 0u;
	constants.shadowMapAvailable = shadowMapAvailable && !useShadow ? 1u : 0u;
	constants.shadowMapLightIndex = directionalShadows_.GetState().lightIndex;
	constants.reflectionFeatureActive =
		!context.postProcessCamera.surfaceSettings.reflectionEnabled ||
		(runtimeFeatures.useDispatchRays && tlasAvailable) ? 1u : 0u;
	constants.reflectionIntensity = context.postProcessCamera.surfaceSettings.reflectionIntensity;
	constants.ambientOcclusionIntensity = context.ambientOcclusion ?
		context.postProcessCamera.surfaceSettings.ambientOcclusionIntensity : 0.0f;
	// 遮蔽がないViewは白Textureを使う
	if (context.ambientOcclusion) {
		context.ambientOcclusion->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		BindGBufferSRV(commandList, ambientOcclusionSlot_, context.ambientOcclusion);
	} else {
		RootBindingCommand::SetGraphicsSRV(commandList, bindCache_.Get(ambientOcclusionSlot_), 0,
			graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture()->gpuHandle);
	}
	constants.shadowViewProjections = directionalShadows_.GetState().viewProjections;
	constants.shadowCascadeSplits = directionalShadows_.GetState().cascadeSplits;
	constants.shadowDepthRanges = directionalShadows_.GetState().depthRanges;
	constants.viewportWidth = sceneFinal->GetWidth();
	constants.viewportHeight = sceneFinal->GetHeight();

	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (camera && camera->valid) {

		constants.cameraPos = camera->cameraPos;
		constants.inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
		constants.viewMatrix = camera->matrices.viewMatrix;
		constants.viewProjectionMatrix = camera->matrices.viewProjectionMatrix;
	} else {

		// 透視カメラが無い場合はskyboxを出さずambientと発光だけにする
		constants.hasSkybox = 0;
		constants.skyboxColor = graphicsCore.GetContext().GetGraphicsSetting().sceneClearColor;
	}

	DxConstBuffer<LightingConstants>& buffer = AllocateConstantBuffer(graphicsCore);
	buffer.TransferData(constants);
	if (bindCache_.Has(constantsSlot_)) {

		RootBindingCommand::SetGraphicsCBV(
			commandList, bindCache_.Get(constantsSlot_), buffer.GetResource()->GetGPUVirtualAddress());
	}

	// 画面全体の三角形を描いてGBufferを合成する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
	if (volumetricShadows) PublishDirectionalShadows(graphicsCore, context, shadowMapAvailable);
	// 後段の粒子も同じSkyboxの環境光で照らす
	ParticleEnvironmentConstants environment;
	environment.colorIntensity = Vector4(skybox.color.r, skybox.color.g, skybox.color.b, skybox.iblIntensity);
	environment.irradianceIndex = irradianceCubemapIndex;
	environment.specularIndex = skybox.specularIndex != UINT32_MAX ? skybox.specularIndex : skybox.cubemapIndex;
	environment.brdfIndex = skybox.brdfIndex;
	environment.physicalEnvironment = skybox.physicalEnvironmentEnabled ? 1u : 0u;
	particleEnvironmentConstants_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), graphicsCore.GetDXObject().GetDevice());
	particleEnvironmentConstants_.Upload(environment);
	context.bufferRegistry.Register({.alias = "ParticleEnvironmentConstants", .resource = nullptr,
		.gpuAddress = particleEnvironmentConstants_.GetGPUAddress()});
	profiler.EndPass(commandList);
}

bool Engine::LightingPass::RequiresDirectionalShadowMap(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
	const SceneExecutionContext& context) {

	if (deps_.renderBatch) for (const auto& bucket : passBuckets.buckets) {
		if (ParticleRenderRequirements::RequiresDirectionalShadows(*deps_.renderBatch, bucket.items)) return true;
	}

	if (!context.renderPassesRuntime) return false;
	const auto features = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const ShadowRequirementKey key{context.renderPassesRuntime, context.renderPassesGeneration,
		context.view->historyWorldRevision, RenderFeatureRuntimeOverrides::GetInstance().GetPlanRevision(),
		*deps_.assetLibrary->GetMaterialRevision(), deps_.pipelineCache->GetContentRevision(),
		uint32_t(features.useMeshShader) | (uint32_t(features.useInlineRayTracing) << 1u) |
		(uint32_t(features.useDispatchRays) << 2u) | (uint32_t(features.useRaytracingDownsampling) << 3u)};
	auto& cached = shadowRequirements_[context.view->GetHistoryKey()];
	if (cached.valid && cached.key == key) return cached.required;
	// 編集とShaderの世代が同じ間は入力一覧を調べ直さない
	cached = {key, false, true};
	for (const auto anchor : {RenderFeatureAnchor::AfterLighting, RenderFeatureAnchor::BeforeTransparent,
		RenderFeatureAnchor::AfterTransparent, RenderFeatureAnchor::AfterPostProcessUI,
		RenderFeatureAnchor::BeforeBlit, RenderFeatureAnchor::AfterToneMap}) {
		const auto plan = context.renderPassesRuntime->GetPlan(anchor, context.kind);
		if (!plan->IsValid()) continue;
		for (const auto& node : plan->nodes) {
			if (!node.pass || node.pass->type != RenderFeaturePassType::Compute ||
				!RenderFeatureRuntimeOverrides::GetInstance().IsEnabled(node.pass->id, node.pass->enabled)) continue;
			const auto* material = deps_.assetLibrary->LoadMaterial(node.pass->material);
			const auto* pass = material ? FindPass(*material, node.pass->materialPass) : nullptr;
			if (!pass) { cached.valid = false; continue; }
			PipelineStaticSamplerOverrideSet samplers;
			samplers.fillMissingSamplers = true;
			samplers.byName = node.pass->samplerOverrides;
			const auto* pipeline = deps_.pipelineCache->GetORCreate(graphicsCore.GetDXObject(), *deps_.assetLibrary,
				pass->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN, features,
				nullptr, false, &samplers, pass->shaderOverride);
			if (!pipeline) { cached.valid = false; continue; }
			if (pipeline->FindBindingByName("DirectionalShadowConstants", ShaderBindingKind::CBV)) {
				cached.required = true;
				return true;
			}
		}
	}
	return false;
}

void Engine::LightingPass::PublishDirectionalShadows(GraphicsCore& graphicsCore, SceneExecutionContext& context, bool available) {

	DirectionalShadowConstants constants;
	constants.viewProjections = directionalShadows_.GetState().viewProjections;
	constants.cascadeSplits = directionalShadows_.GetState().cascadeSplits;
	constants.available = available ? 1u : 0u;
	constants.lightIndex = directionalShadows_.GetState().lightIndex;
	directionalShadowConstants_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), graphicsCore.GetDXObject().GetDevice());
	directionalShadowConstants_.Upload(constants);
	context.bufferRegistry.Register({.alias = "DirectionalShadowConstants", .resource = nullptr,
		.gpuAddress = directionalShadowConstants_.GetGPUAddress()});
	for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
		auto* depth = available ? directionalShadows_.GetDepthTexture(cascade) : nullptr;
		if (depth) depth->Transition(*graphicsCore.GetDXObject().GetDxCommand(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.bufferRegistry.Register({.alias = "gDirectionalShadowMap" + std::to_string(cascade), .resource = nullptr,
			.srvGPUHandle = depth ? depth->GetSRVGPUHandle() : graphicsCore.GetBuiltinTextureLibrary().GetWhiteTexture()->gpuHandle});
	}
}
