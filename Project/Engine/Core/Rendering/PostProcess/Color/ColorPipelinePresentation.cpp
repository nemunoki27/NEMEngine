#include "ColorPipelineProcessor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>

// c++
#include <span>

//============================================================================
//	ColorPipelineProcessor classMethods
//============================================================================
bool Engine::ColorPipelineProcessor::PresentToBackBuffer(
	GraphicsCore& graphicsCore, MultiRenderTarget* source,
	RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache) {

	if (!source || !source->GetColorTexture(0)) return false;
	source->TransitionForShaderRead(*graphicsCore.GetDXObject().GetDxCommand());
	const auto& output = graphicsCore.GetDisplayOutputSettings();
	return PresentTexture(graphicsCore, graphicsCore.GetDXObject().GetDxCommand()->GetCommandList(),
		source->GetColorTexture(0)->GetSRVGPUHandle(), graphicsCore.GetBackBufferRenderTarget(),
		output, output.paperWhiteNits, assetLibrary, pipelineCache);
}

bool Engine::ColorPipelineProcessor::PresentTexture(GraphicsCore& graphicsCore, ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE source, const RenderTarget& target, const DisplayOutputSettings& output,
	float sourceWhiteNits, RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache) {

	// UI合成済み画像と製品の映像に同じ出力変換を適用する
	const MaterialAsset* material = assetLibrary.LoadMaterial(
		BuiltinAssets::Materials::OutputTransform);
	if (!material) {
		return false;
	}
	const MaterialPassBinding* pass = FindPass(*material,
		MaterialPassKind::Blit);
	if (!pass) {
		pass = FindPass(*material, MaterialPassKind::Fullscreen);
	}
	if (!pass || pass->preferredVariant == PipelineVariantKind::Compute ||
		pass->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	const DXGI_FORMAT backBufferFormat = target.format;
	const PipelineState* pipeline = pipelineCache.GetORCreate(
		graphicsCore.GetDXObject(), assetLibrary, pass->pipeline,
		pass->preferredVariant,
		std::span<const DXGI_FORMAT>(&backBufferFormat, 1),
		DXGI_FORMAT_UNKNOWN);
	if (!pipeline || !pipeline->GetGraphicsPipeline(BlendMode::Normal)) {
		return false;
	}

	OutputTransformConstants constants{};
	constants.outputMode = static_cast<uint32_t>(output.mode);
	constants.paperWhiteNits = sourceWhiteNits;
	constants.maxLuminanceNits = output.maxLuminanceNits;
	const FrameConstantBufferAllocation allocation =
		constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), constants);
	if (!allocation.gpuAddress) {
		return false;
	}

	ID3D12DescriptorHeap* heaps[] = { graphicsCore.GetSRVDescriptor().GetDescriptorHeap() };
	commandList->SetDescriptorHeaps(1, heaps);
	commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState(
		pipeline->GetGraphicsPipeline(BlendMode::Normal));

	outputTransformBindingCache_.Sync(*pipeline);
	if (!outputTransformBindingCache_.Has(outputTransformConstantsSlot_) ||
		!outputTransformBindingCache_.Has(outputTransformSourceSlot_)) {
		return false;
	}
	RootBindingCommand::SetGraphicsCBV(commandList,
		outputTransformBindingCache_.Get(outputTransformConstantsSlot_),
		allocation.gpuAddress);
	RootBindingCommand::SetGraphicsSRV(commandList,
		outputTransformBindingCache_.Get(outputTransformSourceSlot_), 0,
		source);

	commandList->OMSetRenderTargets(1, &target.rtvHandle, FALSE, nullptr);
	const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f };
	const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(target.width), static_cast<LONG>(target.height) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
	return true;
}
