#include "SkyboxPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	SkyboxPass classMethods
//============================================================================
void Engine::SkyboxPass::EnsurePipeline(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	desc.preRaster.file = "5b1c0a7e3f9d2486";
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	// cubemapをbindlessで引くためPixelはSM6_6を使う
	desc.pixel.file = "7e2f4a9c1d8b6053";
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

	// 背景なので深度は使わずOpaqueが後から上書きする
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = { 1, 0 };
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;

	initialized_ = pipeline_.CreateGraphics(device, compiler, desc);
}

Engine::DxConstBuffer<Engine::SkyboxPass::SkyboxConstants>& Engine::SkyboxPass::AllocateConstantBuffer(
	GraphicsCore& graphicsCore) {

	// BeginFrameを持たないため、上書き前にGPUが消費し終える程度のリングで回す
	constexpr size_t kRingBufferCount = 8;
	while (constantBuffers_.size() < kRingBufferCount) {

		auto buffer = std::make_unique<DxConstBuffer<SkyboxConstants>>();
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetDevice());
		constantBuffers_.push_back(std::move(buffer));
	}

	DxConstBuffer<SkyboxConstants>& buffer = *constantBuffers_[constantBufferIndex_];
	constantBufferIndex_ = (constantBufferIndex_ + 1u) % static_cast<uint32_t>(constantBuffers_.size());
	return buffer;
}

void Engine::SkyboxPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.world || !context.resources || !context.view) {
		return;
	}

	// 有効なSkyboxを探す、最初に見つかった1件だけを背景に使う
	SkyboxRendererComponent* skybox = nullptr;
	context.world->ForEach<SkyboxRendererComponent>([&](Entity entity, SkyboxRendererComponent& component) {

		if (skybox || !component.visible || !component.cubemapTexture) {
			return;
		}
		if (!IsEntityActiveInHierarchy(*context.world, entity)) {
			return;
		}
		skybox = &component;
		});
	if (!skybox) {
		return;
	}

	// 視点はPerspectiveカメラを使う、2Dビューでは描かない
	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) {
		return;
	}

	// cubemapのSRV indexをbindless用に解決する
	const GPUTextureResource* cubemap = RuntimeTextureResolver::Resolve(
		graphicsCore, context.assetDatabase, skybox->cubemapTexture, false);
	if (!cubemap || cubemap->srvIndex == UINT32_MAX) {
		return;
	}

	MultiRenderTarget* surface = context.resources->GetSceneMain();
	if (!surface || surface->GetWidth() == 0 || surface->GetHeight() == 0) {
		return;
	}
	RenderTexture2D* color = surface->GetColorTexture(0);
	if (!color) {
		return;
	}

	EnsurePipeline(graphicsCore);
	if (!initialized_) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// SceneMainのcolor0だけへ深度無しで描く
	surface->TransitionForRender(*dxCommand);
	dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), std::nullopt);
	dxCommand->SetViewportAndScissor(surface->GetWidth(), surface->GetHeight());

	SkyboxConstants constants{};
	constants.inverseViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
	constants.cameraPosition = camera->cameraPos;
	constants.cubemapIndex = cubemap->srvIndex;
	constants.color = skybox->color;

	DxConstBuffer<SkyboxConstants>& buffer = AllocateConstantBuffer(graphicsCore);
	buffer.TransferData(constants);

	commandList->SetGraphicsRootSignature(pipeline_.GetRootSignature());
	commandList->SetPipelineState(pipeline_.GetGraphicsPipeline(BlendMode::Normal));

	bindCache_.Sync(pipeline_);
	if (bindCache_.Has(cbvSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, bindCache_.Get(cbvSlot_),
			buffer.GetResource()->GetGPUVirtualAddress());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
