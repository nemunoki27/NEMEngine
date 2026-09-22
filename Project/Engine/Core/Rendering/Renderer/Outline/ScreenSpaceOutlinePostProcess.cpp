#include "ScreenSpaceOutlinePostProcess.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxGPUEventScope.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderBackendCapabilities.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineConstants.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

using namespace Engine;

//============================================================================
//	ScreenSpaceOutlineRenderer classMethods
//============================================================================

namespace {

	constexpr Engine::MaterialPassKind kDilateHorizontalPassKind =
		Engine::MaterialPassKind::ScreenSpaceOutlineDilateHorizontal;
	constexpr Engine::MaterialPassKind kDilateVerticalPassKind =
		Engine::MaterialPassKind::ScreenSpaceOutlineDilateVertical;
	constexpr Engine::MaterialPassKind kCompositePassKind =
		Engine::MaterialPassKind::ScreenSpaceOutlineComposite;

	RenderTexture2D* GetColor0(MultiRenderTarget* target) {

		if (!target || target->GetColorCount() == 0) {
			return nullptr;
		}
		return target->GetColorTexture(0);
	}

	const MaterialPassBinding* FindBuiltinPass(RenderAssetLibrary& assetLibrary,
		AssetID materialID, MaterialPassKind passKind) {

		const MaterialAsset* material = assetLibrary.LoadMaterial(materialID);
		if (!material) {
			return nullptr;
		}
		return FindPass(*material, passKind);
	}
}

ScreenSpaceOutlinePostProcess::ScreenSpaceOutlinePostProcess() {

	dilateConstantsCBVSlot_ = dilateBindCache_.AddSlot("DilateConstants", ShaderBindingKind::CBV);
	dilateInputMaskSRVSlot_ = dilateBindCache_.AddSlot("gInputMask", ShaderBindingKind::SRV);
	dilateStylesSRVSlot_ = dilateBindCache_.AddSlot("gOutlineStyles", ShaderBindingKind::SRV);
	dilateOutputUAVSlot_ = dilateBindCache_.AddSlot("gOutputMask", ShaderBindingKind::UAV);

	compositeConstantsCBVSlot_ = compositeBindCache_.AddSlot("CompositeConstants", ShaderBindingKind::CBV);
	compositeMaskSRVSlot_ = compositeBindCache_.AddSlot("gOutlineMask", ShaderBindingKind::SRV);
	compositeDilatedMaskSRVSlot_ = compositeBindCache_.AddSlot("gDilatedOutlineMask", ShaderBindingKind::SRV);
	compositeProjectedCoverageMaskSRVSlot_ = compositeBindCache_.AddSlot("gProjectedCoverageMask", ShaderBindingKind::SRV);
	compositeStylesSRVSlot_ = compositeBindCache_.AddSlot("gOutlineStyles", ShaderBindingKind::SRV);
}

void Engine::ScreenSpaceOutlinePostProcess::Init(GraphicsCore& graphicsCore) {

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	styleBuffer_.Init(device, &graphicsCore.GetSRVDescriptor());
	dilateConstants_.Init(device);
	compositeConstants_.Init(device);
}

void Engine::ScreenSpaceOutlinePostProcess::Release() {

	styleBuffer_.Release();
}

void Engine::ScreenSpaceOutlinePostProcess::UploadStyles(std::span<const ScreenSpaceOutlineStyleGPU> styles) {

	styleBuffer_.Upload(styles);
}

bool ScreenSpaceOutlinePostProcess::ValidateDilationResources(
	const ScreenSpaceOutlineViewResources& resources) {

	return GetColor0(resources.mask.get()) != nullptr &&
		GetColor0(resources.horizontalDilatedMask.get()) != nullptr &&
		GetColor0(resources.dilatedMask.get()) != nullptr;
}

bool ScreenSpaceOutlinePostProcess::ExecuteDilation(GraphicsCore& graphicsCore,
	const RenderPipelineDeps& deps, ScreenSpaceOutlineViewResources& resources, uint32_t maxRadiusPixels, uint32_t styleCount) {

	if (!ValidateDilationResources(resources)) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のResourceが不正です");
		return false;
	}

	// 半径は必ず共通上限でclampしShader側の上限と揃える
	const uint32_t safeRadius = (std::min)(maxRadiusPixels, kMaxScreenSpaceOutlineRadiusPixels);

	// 1つのDilation Materialから横/縦のpassを引く
	const MaterialPassBinding* horizontalPass = FindBuiltinPass(
		*deps.assetLibrary, BuiltinAssets::Materials::ScreenSpaceOutlineDilate, kDilateHorizontalPassKind);
	const MaterialPassBinding* verticalPass = FindBuiltinPass(
		*deps.assetLibrary, BuiltinAssets::Materials::ScreenSpaceOutlineDilate, kDilateVerticalPassKind);
	if (!horizontalPass || !verticalPass ||
		horizontalPass->preferredVariant != PipelineVariantKind::Compute ||
		verticalPass->preferredVariant != PipelineVariantKind::Compute) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のPassがありません");
		return false;
	}

	RenderTexture2D* mask = GetColor0(resources.mask.get());
	RenderTexture2D* horizontalMask = GetColor0(resources.horizontalDilatedMask.get());
	RenderTexture2D* dilatedMask = GetColor0(resources.dilatedMask.get());

	// Horizontal: mask(SRV) -> horizontalDilatedMask(UAV) -> NON_PIXEL_SHADER_RESOURCE
	if (!ExecuteDilationPass(graphicsCore, deps, horizontalPass->pipeline, resources,
		mask, horizontalMask, safeRadius, false, L"SSOutline.Dilation.Horizontal", styleCount)) {
		return false;
	}
	// Vertical: horizontalDilatedMask(SRV) -> dilatedMask(UAV) -> PIXEL_SHADER_RESOURCE
	if (!ExecuteDilationPass(graphicsCore, deps, verticalPass->pipeline, resources,
		horizontalMask, dilatedMask, safeRadius, true, L"SSOutline.Dilation.Vertical", styleCount)) {
		return false;
	}
	return true;
}

bool ScreenSpaceOutlinePostProcess::ExecuteDilationPass(GraphicsCore& graphicsCore,
	const RenderPipelineDeps& deps, AssetID pipelineID, ScreenSpaceOutlineViewResources& resources,
	RenderTexture2D* inputMask, RenderTexture2D* outputMask, uint32_t safeRadius, bool finalToPixelShader,
	const wchar_t* label, uint32_t styleCount) {

	if (!inputMask || !outputMask) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のTextureがNullです");
		return false;
	}

	const PipelineState* pipelineState = deps.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps.assetLibrary, pipelineID, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のPipelineがありません");
		return false;
	}

	// thread group size /解像度/ handleが揃わなければDispatchしない(GPU Hang・不正アクセス防止)
	const uint32_t threadGroupX = pipelineState->GetThreadGroupX();
	const uint32_t threadGroupY = pipelineState->GetThreadGroupY();
	const uint32_t width = resources.mask->GetWidth();
	const uint32_t height = resources.mask->GetHeight();
	if (threadGroupX == 0u || threadGroupY == 0u || width == 0u || height == 0u) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のThread Groupまたは描画サイズが0です");
		return false;
	}
	if (inputMask->GetSRVGPUHandle().ptr == 0 || outputMask->GetUAVGPUHandle().ptr == 0 ||
		styleBuffer_.GetGPUHandle().ptr == 0) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のResource HandleがNullです");
		return false;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	DxGPUEventScope eventScope{ commandList, label };

	// 入力はSRVで出力はUAVへ遷移し前段や前frameの状態へ依存させない
	inputMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	outputMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	ScreenSpaceOutlineDilateConstants constants{};
	constants.width = width;
	constants.height = height;
	constants.styleCount = styleCount;
	constants.maxRadiusPixels = safeRadius;
	dilateConstants_.Upload(constants);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	dilateBindCache_.Sync(*pipelineState);
	const bool hasAllBindings =
		dilateBindCache_.Has(dilateConstantsCBVSlot_) &&
		dilateBindCache_.Has(dilateInputMaskSRVSlot_) &&
		dilateBindCache_.Has(dilateStylesSRVSlot_) &&
		dilateBindCache_.Has(dilateOutputUAVSlot_);
	if (!hasAllBindings) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 膨張処理のBindingが不足しています");
		return false;
	}

	RootBindingCommand::SetComputeCBV(commandList, dilateBindCache_.Get(dilateConstantsCBVSlot_),
		dilateConstants_.GetGPUAddress());
	RootBindingCommand::SetComputeSRV(commandList, dilateBindCache_.Get(dilateInputMaskSRVSlot_),
		0, inputMask->GetSRVGPUHandle());
	RootBindingCommand::SetComputeSRV(commandList, dilateBindCache_.Get(dilateStylesSRVSlot_),
		styleBuffer_.GetGPUAddress(), styleBuffer_.GetGPUHandle());
	RootBindingCommand::SetComputeUAV(commandList, dilateBindCache_.Get(dilateOutputUAVSlot_),
		0, outputMask->GetUAVGPUHandle());

	const UINT dispatchX = DxUtils::RoundUp(width, threadGroupX);
	const UINT dispatchY = DxUtils::RoundUp(height, threadGroupY);
	commandList->Dispatch(dispatchX, dispatchY, 1);

	// computeの書き込み完了を保証してから次段/compositeで読む
	dxCommand->UAVBarrier(outputMask->GetResource());
	outputMask->Transition(*dxCommand, finalToPixelShader ?
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	return true;
}

bool ScreenSpaceOutlinePostProcess::ExecuteComposite(GraphicsCore& graphicsCore,
	SceneExecutionContext& context, const RenderPipelineDeps& deps,
	ScreenSpaceOutlineViewResources& resources, MultiRenderTarget* compositeTarget, uint32_t styleCount) {

	if (!compositeTarget) {
		return false;
	}

	RenderTexture2D* mask = GetColor0(resources.mask.get());
	RenderTexture2D* projectedCoverageMask = GetColor0(resources.projectedCoverageMask.get());
	RenderTexture2D* dilatedMask = GetColor0(resources.dilatedMask.get());
	if (!mask || !projectedCoverageMask || !dilatedMask || compositeTarget->GetColorCount() == 0) {
		return false;
	}

	const MaterialPassBinding* passBinding = FindBuiltinPass(
		*deps.assetLibrary, BuiltinAssets::Materials::ScreenSpaceOutlineComposite, kCompositePassKind);
	if (!passBinding || passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	std::array<DXGI_FORMAT, 8> rtvFormats{};
	rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
	uint32_t formatCount = 0;
	for (uint32_t i = 0; i <
		(std::min)(compositeTarget->GetColorCount(), static_cast<uint32_t>(rtvFormats.size())); ++i) {
		if (const RenderTexture2D* color = compositeTarget->GetColorTexture(i)) {
			rtvFormats[formatCount++] = color->GetFormat();
		}
	}

	const PipelineState* pipelineState = deps.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps.assetLibrary, passBinding->pipeline, PipelineVariantKind::GraphicsVertex,
		std::span<const DXGI_FORMAT>(rtvFormats.data(), formatCount), DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetGraphicsPipeline(BlendMode::Normal)) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] 合成Pipelineがありません");
		return false;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	DxGPUEventScope eventScope{ commandList, L"SSOutline.Composite" };

	// styleID範囲外読みを防ぐため、現在のstyle数をcomposite側へ渡す
	ScreenSpaceOutlineCompositeConstants compositeConstants{};
	compositeConstants.styleCount = styleCount;
	compositeConstants_.Upload(compositeConstants);

	mask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	projectedCoverageMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	dilatedMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	compositeTarget->TransitionForRender(*dxCommand);
	compositeTarget->Bind(*dxCommand);
	if (context.useViewportRect) {
		dxCommand->SetViewportAndScissor(
			context.viewportX, context.viewportY,
			context.viewportWidth, context.viewportHeight);
	}

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(BlendMode::Normal));

	compositeBindCache_.Sync(*pipelineState);
	if (compositeBindCache_.Has(compositeConstantsCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, compositeBindCache_.Get(compositeConstantsCBVSlot_),
			compositeConstants_.GetGPUAddress());
	}
	if (compositeBindCache_.Has(compositeMaskSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, compositeBindCache_.Get(compositeMaskSRVSlot_),
			0, mask->GetSRVGPUHandle());
	}
	if (compositeBindCache_.Has(compositeDilatedMaskSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, compositeBindCache_.Get(compositeDilatedMaskSRVSlot_),
			0, dilatedMask->GetSRVGPUHandle());
	}
	if (compositeBindCache_.Has(compositeProjectedCoverageMaskSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, compositeBindCache_.Get(compositeProjectedCoverageMaskSRVSlot_),
			0, projectedCoverageMask->GetSRVGPUHandle());
	}
	if (compositeBindCache_.Has(compositeStylesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, compositeBindCache_.Get(compositeStylesSRVSlot_),
			styleBuffer_.GetGPUAddress(), styleBuffer_.GetGPUHandle());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
	return true;
}
