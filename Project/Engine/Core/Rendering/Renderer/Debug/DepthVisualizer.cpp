#include "DepthVisualizer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// c++
#include <cmath>

//============================================================================
//	DepthVisualizer classMethods
//============================================================================

Engine::DepthVisualizer::DepthVisualizer() {

	// 可視化用定数はb0、深度SRVはt0で受ける
	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
	depthSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
}

void Engine::DepthVisualizer::EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat) {

	if (initialized_) {
		return;
	}

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	// 画面全体を覆う共通VSを使い回す
	desc.preRaster.file = "Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl";
	desc.preRaster.shader = BuiltinAssets::Shaders::FullscreenCopy;
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	desc.pixel.file = "Builtin/Debug/depthVisualize.PS.hlsl";
	desc.pixel.shader = BuiltinAssets::Shaders::DepthVisualize;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";

	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

	// 全画面描画では深度を使わない
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = { 1, 0 };
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = colorFormat;
	desc.dsvFormat = DXGI_FORMAT_UNKNOWN;

	initialized_ = pipeline_.CreateGraphics(
		graphicsCore.GetDXObject().GetDevice(),
		graphicsCore.GetDXObject().GetDxShaderCompiler(), desc);
	if (initialized_) {
		for (DxConstBuffer<DepthVisualizeConstants>& buffer : constantBuffers_) {
			buffer.CreateBuffer(graphicsCore.GetDXObject().GetDevice());
		}
	}
}

Engine::RenderTexture2D* Engine::DepthVisualizer::Render(
	GraphicsCore& graphicsCore, const ResolvedCameraView& camera,
	DepthTexture2D* depth, MultiRenderTarget& output) {

	if (!depth || !depth->IsValid() || !output.IsValid() || output.GetColorCount() == 0) {
		return nullptr;
	}

	RenderTexture2D* color = output.GetColorTexture(0);
	if (!color) {
		return nullptr;
	}

	EnsurePipeline(graphicsCore, color->GetFormat());
	if (!initialized_) {
		return nullptr;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList* commandList = dxCommand->GetCommandList();
	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	DxConstBuffer<DepthVisualizeConstants>& constantBuffer =
		constantBuffers_[frameIndex];
	if (!constantBuffer.IsCreatedResource()) {
		return nullptr;
	}

	DepthVisualizeConstants constants{};
	constants.projectionA = camera.matrices.projectionMatrix.m[2][2];
	constants.projectionB = camera.matrices.projectionMatrix.m[3][2];
	constants.nearClip = camera.nearClip;
	constants.farClip = camera.farClip;
	constants.perspective =
		std::abs(camera.matrices.projectionMatrix.m[2][3]) > 0.5f;
	constantBuffer.TransferData(constants);

	// 入力深度を読み取り、出力色をレンダーターゲットへ遷移する
	depth->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	output.TransitionForRender(*dxCommand);
	output.Bind(*dxCommand);
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	commandList->SetGraphicsRootSignature(pipeline_.GetRootSignature());
	commandList->SetPipelineState(pipeline_.GetGraphicsPipeline(BlendMode::Normal));

	bindCache_.Sync(pipeline_);
	if (!bindCache_.Has(constantsSlot_) || !bindCache_.Has(depthSlot_)) {
		return nullptr;
	}
	RootBindingCommand::SetGraphicsCBV(commandList,
		bindCache_.Get(constantsSlot_),
		constantBuffer.GetResource()->GetGPUVirtualAddress());
	RootBindingCommand::SetGraphicsSRV(
		commandList, bindCache_.Get(depthSlot_), 0, depth->GetSRVGPUHandle());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	// ImGuiが同じフレームで参照できるよう出力をSRVへ戻す
	output.TransitionForShaderRead(*dxCommand);
	return color;
}
