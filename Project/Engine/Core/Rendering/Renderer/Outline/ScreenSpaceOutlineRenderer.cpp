#include "ScreenSpaceOutlineRenderer.h"

using namespace Engine;

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

//============================================================================
//	ScreenSpaceOutlineRenderer classMethods
//============================================================================

namespace {

	constexpr Engine::MaterialPassKind kMaskPassKind = Engine::MaterialPassKind::ScreenSpaceOutlineMask;
	constexpr Engine::MaterialPassKind kCoverageMaskPassKind = Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask;
	constexpr Engine::MaterialPassKind kDilateHorizontalPassKind = Engine::MaterialPassKind::ScreenSpaceOutlineDilateHorizontal;
	constexpr Engine::MaterialPassKind kDilateVerticalPassKind = Engine::MaterialPassKind::ScreenSpaceOutlineDilateVertical;
	constexpr Engine::MaterialPassKind kCompositePassKind = Engine::MaterialPassKind::ScreenSpaceOutlineComposite;

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

	bool IsSameEntity(const RenderItem& item, const ScreenSpaceOutlineRequest& request) {

		return item.world == request.world &&
			item.entity.index == request.entity.index &&
			item.entity.generation == request.entity.generation;
	}
	// styleが完全一致するか、複数選択でstyleIDを共有して描画パスをまとめるために使う
	bool IsSameOutlineStyleGPU(const ScreenSpaceOutlineStyleGPU& a, const ScreenSpaceOutlineStyleGPU& b) {

		return a.color.r == b.color.r && a.color.g == b.color.g &&
			a.color.b == b.color.b && a.color.a == b.color.a &&
			a.widthPixels == b.widthPixels && a.priority == b.priority &&
			a.visibilityMode == b.visibilityMode && a.regionMode == b.regionMode;
	}
}

ScreenSpaceOutlineRenderer::ScreenSpaceOutlineRenderer() {

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

ScreenSpaceOutlineRenderer::~ScreenSpaceOutlineRenderer() {

	Finalize();
}

void ScreenSpaceOutlineRenderer::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	styleBuffer_.Init(device, &graphicsCore.GetSRVDescriptor());
	dilateConstants_.Init(device);
	compositeConstants_.Init(device);

	initialized_ = true;
}

void ScreenSpaceOutlineRenderer::Finalize() {

	styleBuffer_.Release();
	styleScratch_.clear();
	drawScratch_.clear();
	itemScratch_.clear();
	initialized_ = false;
}

void ScreenSpaceOutlineRenderer::Render(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
	std::span<const ScreenSpaceOutlineRequest> requests, ScreenSpaceOutlineViewResources& resources) {

	if (!context.resources || !resources.IsValid() || !deps.assetLibrary || !deps.pipelineCache ||
		!deps.dispatcher || !deps.renderBatch || !deps.backendRegistry || !deps.materialResolver) {
		return;
	}

	uint32_t maxRadiusPixels = 0;
	if (!BuildDrawRecords(requests, maxRadiusPixels)) {
		return;
	}

	Init(graphicsCore);
	styleBuffer_.Upload(styleScratch_);

	ClearMask(graphicsCore, resources);
	DrawMask(graphicsCore, context, passBuckets, deps, resources);
	if (!ExecuteDilation(graphicsCore, deps, resources, maxRadiusPixels)) {
		return;
	}
	ExecuteComposite(graphicsCore, context, deps, resources);
}

bool ScreenSpaceOutlineRenderer::BuildDrawRecords(
	std::span<const ScreenSpaceOutlineRequest> requests, uint32_t& outMaxRadiusPixels) {

	styleScratch_.clear();
	drawScratch_.clear();
	outMaxRadiusPixels = 0;

	for (const ScreenSpaceOutlineRequest& request : requests) {

		if (!request.world || !request.entity.IsValid()) {
			continue;
		}
		if (!std::isfinite(request.style.widthPixels) || request.style.widthPixels <= 0.0f) {
			continue;
		}

		// GPUへ渡す前に必ず半径上限でclampする、巨大値はDilationのGPU Hang原因になる
		const float width = std::clamp(request.style.widthPixels, 0.0f,
			static_cast<float>(kMaxScreenSpaceOutlineRadiusPixels));
		ScreenSpaceOutlineStyleGPU gpuStyle{};
		gpuStyle.color = request.style.color;
		gpuStyle.widthPixels = width;
		gpuStyle.priority = request.style.priority;
		gpuStyle.visibilityMode = static_cast<uint32_t>(request.style.visibilityMode);
		gpuStyle.regionMode = static_cast<uint32_t>(request.style.regionMode);

		// 同一styleは集約してstyleIDを共有する、複数選択で同じ見た目なら1枠で済む
		uint32_t styleID = 0;
		for (size_t i = 0; i < styleScratch_.size(); ++i) {
			if (IsSameOutlineStyleGPU(styleScratch_[i], gpuStyle)) {
				styleID = static_cast<uint32_t>(i + 1);
				break;
			}
		}
		if (styleID == 0) {

			if (styleScratch_.size() >= kMaxScreenSpaceOutlineStyles) {

				if (!overflowLogged_) {
					Logger::Output(LogType::Engine, spdlog::level::warn,
						"[ScreenSpaceOutline] style count exceeded {}. Extra requests are ignored.",
						kMaxScreenSpaceOutlineStyles);
					overflowLogged_ = true;
				}
				break;
			}
			styleScratch_.emplace_back(gpuStyle);
			styleID = static_cast<uint32_t>(styleScratch_.size());
		}

		ScreenSpaceOutlineRequest sanitized = request;
		sanitized.style.widthPixels = width;

		DrawRecord record{};
		record.request = sanitized;
		record.styleID = styleID;
		drawScratch_.emplace_back(record);

		outMaxRadiusPixels = (std::max)(outMaxRadiusPixels,
			static_cast<uint32_t>(std::ceil(width)));
	}

	std::sort(drawScratch_.begin(), drawScratch_.end(), [](const DrawRecord& lhs, const DrawRecord& rhs) {

		if (lhs.request.style.priority != rhs.request.style.priority) {
			return lhs.request.style.priority < rhs.request.style.priority;
		}
		// 同priorityは低いStyle IDを安定して優先するため、先に高いIDを描いて低いIDを後勝ちにする
		return lhs.styleID > rhs.styleID;
	});

	return !drawScratch_.empty();
}

void ScreenSpaceOutlineRenderer::ClearMask(
	GraphicsCore& graphicsCore, ScreenSpaceOutlineViewResources& resources) const {

	if (!resources.mask || !resources.projectedCoverageMask) {
		return;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	// Visible Mask
	{
		resources.mask->TransitionForRender(*dxCommand);
		resources.mask->Bind(*dxCommand);

		MultiRenderTargetClearDesc clear{};
		clear.clearColor = true;
		clear.clearColorValue = Color4::Black();
		clear.clearDepth = false;
		clear.clearStencil = false;
		resources.mask->Clear(*dxCommand, clear);
	}

	// 投影カバレッジマスク
	{
		resources.projectedCoverageMask->TransitionForRender(*dxCommand);
		resources.projectedCoverageMask->Bind(*dxCommand);

		MultiRenderTargetClearDesc clear{};
		clear.clearColor = true;
		clear.clearColorValue = Color4::Black();
		clear.clearDepth = false;
		clear.clearStencil = false;
		resources.projectedCoverageMask->Clear(*dxCommand, clear);
	}
}

void ScreenSpaceOutlineRenderer::DrawMask(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
	ScreenSpaceOutlineViewResources& resources) {

	if (!resources.mask || !resources.projectedCoverageMask ||
		!context.resources || !context.resources->GetSceneMain()) {
		return;
	}

	DepthTexture2D* sceneDepth = context.resources->GetSceneMain()->GetDepthTexture();
	if (!sceneDepth) {
		return;
	}

	const RenderPassItemList& list = passBuckets.Get(RenderPhase::Opaque);
	if (list.IsEmpty()) {
		return;
	}

	const uint32_t prevStyleID = context.screenSpaceOutlineMaskStyleID;
	const int32_t prevSubMeshIndex = context.screenSpaceOutlineMaskRestrictSubMeshIndex;

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	DxGPUEventScope eventScope{ commandList, L"SSOutline.MaskDraw" };

	// 同じstyleIDとsubMeshIndexのrecordをまとめて1パスで描く、複数選択でもパス数が増えない
	auto drawGroupedMask = [&](const RenderPassSurfaceBinding& binding, MaterialPassKind passKind, bool coverageOnly) {

		for (size_t groupBegin = 0; groupBegin < drawScratch_.size(); ) {

			const uint32_t groupStyleID = drawScratch_[groupBegin].styleID;
			const int32_t groupSubMeshIndex = drawScratch_[groupBegin].request.subMeshIndex;
			const ScreenSpaceOutlineRegionMode groupRegion = drawScratch_[groupBegin].request.style.regionMode;

			size_t groupEnd = groupBegin;
			while (groupEnd < drawScratch_.size() &&
				drawScratch_[groupEnd].styleID == groupStyleID &&
				drawScratch_[groupEnd].request.subMeshIndex == groupSubMeshIndex) {
				++groupEnd;
			}

			// CoverageはExteriorPreferredのstyleだけ描く、groupは同styleなので一括で判定できる
			if (!coverageOnly || groupRegion == ScreenSpaceOutlineRegionMode::ExteriorPreferred) {

				itemScratch_.clear();
				for (size_t recordIndex = groupBegin; recordIndex < groupEnd; ++recordIndex) {

					for (const RenderItem* item : list.items) {

						// マスクパスを解決できるバックエンドだけを対象にする
						if (!item || !RenderBackendCapabilities::SupportsOutlineMask(item->backendID)) {
							continue;
						}
						if (!IsSameEntity(*item, drawScratch_[recordIndex].request)) {
							continue;
						}
						itemScratch_.emplace_back(item);
					}
				}
				if (!itemScratch_.empty()) {

					context.screenSpaceOutlineMaskStyleID = groupStyleID;
					context.screenSpaceOutlineMaskRestrictSubMeshIndex = groupSubMeshIndex;
					RenderPassExecutionHelper::Execute(graphicsCore, context, itemScratch_, deps,
						binding, passKind, false, false);
				}
			}
			groupBegin = groupEnd;
		}
		};

	// 1. Visible Mask (Depth Testあり)
	{
		RenderPassSurfaceBinding maskBinding{};
		maskBinding.colorSurface = resources.mask.get();
		maskBinding.depthOverride = sceneDepth;
		drawGroupedMask(maskBinding, kMaskPassKind, false);
	}

	// 2. Projected Coverage Mask (Depth Test無し)
	{
		RenderPassSurfaceBinding coverageBinding{};
		coverageBinding.colorSurface = resources.projectedCoverageMask.get();
		// 遮蔽判定を行わないのでDepth不要
		drawGroupedMask(coverageBinding, kCoverageMaskPassKind, true);
	}

	context.screenSpaceOutlineMaskStyleID = prevStyleID;
	context.screenSpaceOutlineMaskRestrictSubMeshIndex = prevSubMeshIndex;
}

bool ScreenSpaceOutlineRenderer::ValidateDilationResources(
	const ScreenSpaceOutlineViewResources& resources) {

	return GetColor0(resources.mask.get()) != nullptr &&
		GetColor0(resources.horizontalDilatedMask.get()) != nullptr &&
		GetColor0(resources.dilatedMask.get()) != nullptr;
}

bool ScreenSpaceOutlineRenderer::ExecuteDilation(GraphicsCore& graphicsCore,
	const RenderPipelineDeps& deps, ScreenSpaceOutlineViewResources& resources, uint32_t maxRadiusPixels) {

	if (!ValidateDilationResources(resources)) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation resources are invalid.");
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
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation passes are missing.");
		return false;
	}

	RenderTexture2D* mask = GetColor0(resources.mask.get());
	RenderTexture2D* horizontalMask = GetColor0(resources.horizontalDilatedMask.get());
	RenderTexture2D* dilatedMask = GetColor0(resources.dilatedMask.get());

	// Horizontal: mask(SRV) -> horizontalDilatedMask(UAV) -> NON_PIXEL_SHADER_RESOURCE
	if (!ExecuteDilationPass(graphicsCore, deps, horizontalPass->pipeline, resources,
		mask, horizontalMask, safeRadius, false, L"SSOutline.Dilation.Horizontal")) {
		return false;
	}
	// Vertical: horizontalDilatedMask(SRV) -> dilatedMask(UAV) -> PIXEL_SHADER_RESOURCE
	if (!ExecuteDilationPass(graphicsCore, deps, verticalPass->pipeline, resources,
		horizontalMask, dilatedMask, safeRadius, true, L"SSOutline.Dilation.Vertical")) {
		return false;
	}
	return true;
}

bool ScreenSpaceOutlineRenderer::ExecuteDilationPass(GraphicsCore& graphicsCore,
	const RenderPipelineDeps& deps, AssetID pipelineID, ScreenSpaceOutlineViewResources& resources,
	RenderTexture2D* inputMask, RenderTexture2D* outputMask, uint32_t safeRadius, bool finalToPixelShader,
	const wchar_t* label) {

	if (!inputMask || !outputMask) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation pass texture is null.");
		return false;
	}

	const PipelineState* pipelineState = deps.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps.assetLibrary, pipelineID, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation pipeline is missing.");
		return false;
	}

	// thread group size /解像度/ handleが揃わなければDispatchしない(GPU Hang・不正アクセス防止)
	const uint32_t threadGroupX = pipelineState->GetThreadGroupX();
	const uint32_t threadGroupY = pipelineState->GetThreadGroupY();
	const uint32_t width = resources.mask->GetWidth();
	const uint32_t height = resources.mask->GetHeight();
	if (threadGroupX == 0u || threadGroupY == 0u || width == 0u || height == 0u) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation thread group or size is zero.");
		return false;
	}
	if (inputMask->GetSRVGPUHandle().ptr == 0 || outputMask->GetUAVGPUHandle().ptr == 0 ||
		styleBuffer_.GetGPUHandle().ptr == 0) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation resource handle is null.");
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
	constants.styleCount = static_cast<uint32_t>(styleScratch_.size());
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
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] dilation bindings are incomplete.");
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

bool ScreenSpaceOutlineRenderer::ExecuteComposite(GraphicsCore& graphicsCore,
	SceneExecutionContext& context, const RenderPipelineDeps& deps,
	ScreenSpaceOutlineViewResources& resources) {

	if (!context.resources || !context.resources->GetSceneFinal()) {
		return false;
	}

	RenderTexture2D* mask = GetColor0(resources.mask.get());
	RenderTexture2D* projectedCoverageMask = GetColor0(resources.projectedCoverageMask.get());
	RenderTexture2D* dilatedMask = GetColor0(resources.dilatedMask.get());
	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!mask || !projectedCoverageMask || !dilatedMask || !sceneFinal || sceneFinal->GetColorCount() == 0) {
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
	for (uint32_t i = 0; i < (std::min)(sceneFinal->GetColorCount(), static_cast<uint32_t>(rtvFormats.size())); ++i) {
		if (const RenderTexture2D* color = sceneFinal->GetColorTexture(i)) {
			rtvFormats[formatCount++] = color->GetFormat();
		}
	}

	const PipelineState* pipelineState = deps.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*deps.assetLibrary, passBinding->pipeline, PipelineVariantKind::GraphicsVertex,
		std::span<const DXGI_FORMAT>(rtvFormats.data(), formatCount), DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetGraphicsPipeline(BlendMode::Normal)) {
		Logger::Output(LogType::Engine, "[ScreenSpaceOutline] composite pipeline is missing.");
		return false;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	DxGPUEventScope eventScope{ commandList, L"SSOutline.Composite" };

	// styleID範囲外読みを防ぐため、現在のstyle数をcomposite側へ渡す
	ScreenSpaceOutlineCompositeConstants compositeConstants{};
	compositeConstants.styleCount = static_cast<uint32_t>(styleScratch_.size());
	compositeConstants_.Upload(compositeConstants);

	mask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	projectedCoverageMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	dilatedMask->Transition(*dxCommand, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	sceneFinal->TransitionForRender(*dxCommand);
	sceneFinal->Bind(*dxCommand);
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
