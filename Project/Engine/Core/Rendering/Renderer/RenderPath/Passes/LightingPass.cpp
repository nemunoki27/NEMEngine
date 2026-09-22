#include "LightingPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>

//============================================================================
//	LightingPass classMethods
//============================================================================

Engine::LightingPass::LightingPass() {

	// GBufferの各アタッチメントをregister順でスロット登録する、t0-t5はSceneMainのcolor並びと一致
	albedoSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
	normalSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 1, 0);
	worldPosSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 2, 0);
	materialSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 3, 0);
	emissiveSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 4, 0);
	flagsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 5, 0);
	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 1, 0);
}

void Engine::LightingPass::EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat) {

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

	// cubemapをbindlessで引くためPixelはSM6_6を使う
	desc.pixel.file = "Builtin/Lighting/deferredLighting.PS.hlsl";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLighting;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_6";

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

	desc.sampleDesc = { 1, 0 };
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = colorFormat;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;

	// シャドウ無し版、gSceneTLASを参照しない
	desc.pixel.entry = "main";
	initialized_ = (pipeline_ = PipelineStateBuilder::CreateGraphics(device, compiler, desc)) != nullptr;

	// TLASシャドウ付き版、inlineRT非対応環境ではPSO構築に失敗するためフラグで持つ
	desc.pixel.entry = "mainShadowed";
	desc.pixel.shader = BuiltinAssets::Shaders::DeferredLightingShadowed;
	shadowedAvailable_ = (pipelineShadowed_ = PipelineStateBuilder::CreateGraphics(device, compiler, desc)) != nullptr;
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
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetDevice());
		buffers.push_back(std::move(buffer));
	}
	return *buffers[bufferIndex++];
}

void Engine::LightingPass::BindGBufferSRV(ID3D12GraphicsCommandList* commandList,
	PipelineBindingCache::SlotID slot, RenderTexture2D* texture) {

	if (!bindCache_.Has(slot) || !texture) {
		return;
	}
	RootBindingCommand::SetGraphicsSRV(commandList, bindCache_.Get(slot), 0, texture->GetSRVGPUHandle());
}

void Engine::LightingPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

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

	EnsurePipeline(graphicsCore, destColor->GetFormat());
	if (!initialized_) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// GBufferをシェーダーリードへ、SceneFinalをレンダーターゲットへ遷移してバインド
	sceneMain->TransitionForShaderRead(*dxCommand);
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// 背景に使うskyboxを探す、最初に見つかったskyboxが対象
	const SceneSkyboxInfo skybox = SceneSkyboxResolver::Resolve(graphicsCore, context.assetDatabase, context.world);
	uint32_t irradianceCubemapIndex = 0xFFFFFFFF;
	if (skybox.found) {

		// 拡散IBL用の放射照度cubemapを更新する、描画PSO設定前にコンピュートを積む
		irradianceMap_.Update(graphicsCore, skybox.cubemapAssetID, skybox.cubemapIndex);
		irradianceCubemapIndex = irradianceMap_.GetSRVIndex();
	}

	// 影付きライトがありinlineRTとTLASを使えるときだけシャドウ付きPSOを選ぶ
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const bool tlasAvailable = context.bufferRegistry.Find("gSceneTLAS") != nullptr;
	const bool useShadow = context.hasShadowCastingLight &&
		shadowedAvailable_ && runtimeFeatures.useInlineRayTracing &&
		tlasAvailable;
	PipelineState& activePipeline = useShadow ? *pipelineShadowed_ : *pipeline_;

	commandList->SetGraphicsRootSignature(activePipeline.GetRootSignature());
	commandList->SetPipelineState(activePipeline.GetGraphicsPipeline(BlendMode::Normal));

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

	// 背景復元用の逆ビュー射影と視点を集める、2Dビューでは背景を出さない
	LightingConstants constants{};
	constants.ambientIntensity = 0.03f;
	constants.skyboxColor = skybox.found ? skybox.color : Color4::FromHex(0x303030ff);
	constants.skyboxCubemapIndex = skybox.found ? skybox.cubemapIndex : 0xFFFFFFFF;
	constants.hasSkybox = skybox.found ? 1u : 0u;
	constants.irradianceCubemapIndex = irradianceCubemapIndex;
	constants.iblIntensity = skybox.iblIntensity;
	constants.softShadowSampleCount =
		runtimeFeatures.softShadowSampleCount;
	constants.viewportWidth = sceneFinal->GetWidth();
	constants.viewportHeight = sceneFinal->GetHeight();

	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (camera && camera->valid) {

		constants.cameraPos = camera->cameraPos;
		constants.inverseViewProjection =
			camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
		constants.viewMatrix = camera->matrices.viewMatrix;
	} else {

		// 透視カメラが無い場合はskyboxを出さずambientと発光だけにする
		constants.hasSkybox = 0;
	}

	DxConstBuffer<LightingConstants>& buffer = AllocateConstantBuffer(graphicsCore);
	buffer.TransferData(constants);
	if (bindCache_.Has(constantsSlot_)) {

		RootBindingCommand::SetGraphicsCBV(commandList, bindCache_.Get(constantsSlot_), buffer.GetResource()->GetGPUVirtualAddress());
	}

	// 画面全体の三角形を描いてGBufferを合成する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
