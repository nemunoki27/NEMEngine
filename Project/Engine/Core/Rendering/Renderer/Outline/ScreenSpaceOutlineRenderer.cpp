#include "ScreenSpaceOutlineRenderer.h"

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

	constexpr Engine::MaterialPassKind kMaskPassKind =
		Engine::MaterialPassKind::ScreenSpaceOutlineMask;
	constexpr Engine::MaterialPassKind kCoverageMaskPassKind =
		Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask;

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

ScreenSpaceOutlineRenderer::ScreenSpaceOutlineRenderer() = default;

ScreenSpaceOutlineRenderer::~ScreenSpaceOutlineRenderer() {

	Finalize();
}

void ScreenSpaceOutlineRenderer::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	postProcess_.Init(graphicsCore);

	initialized_ = true;
}

void ScreenSpaceOutlineRenderer::Finalize() {

	postProcess_.Release();
	styleScratch_.clear();
	drawScratch_.clear();
	itemScratch_.clear();
	initialized_ = false;
}

void ScreenSpaceOutlineRenderer::Render(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	const RenderPassPhaseBuckets& passBuckets, const RenderPipelineDeps& deps,
	std::span<const ScreenSpaceOutlineRequest> requests, ScreenSpaceOutlineViewResources& resources,
	std::span<const RenderPhase> phases, MultiRenderTarget* compositeTarget,
	DepthTexture2D* depthOverride) {

	if (!context.resources || !resources.IsValid() || !deps.assetLibrary || !deps.pipelineCache ||
		!deps.dispatcher || !deps.renderBatch || !deps.backendRegistry || !deps.materialResolver ||
		phases.empty() || !compositeTarget) {
		return;
	}

	uint32_t maxRadiusPixels = 0;
	if (!BuildDrawRecords(requests, maxRadiusPixels)) {
		return;
	}

	Init(graphicsCore);
	postProcess_.UploadStyles(styleScratch_);

	ClearMask(graphicsCore, resources);
	DrawMask(graphicsCore, context, passBuckets, deps, resources, phases, depthOverride);
	if (!postProcess_.ExecuteDilation(graphicsCore, deps, resources, maxRadiusPixels, static_cast<uint32_t>(styleScratch_.size()))) {
		return;
	}
	postProcess_.ExecuteComposite(graphicsCore, context, deps, resources, compositeTarget, static_cast<uint32_t>(styleScratch_.size()));
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
						"[ScreenSpaceOutline] Style数が上限{}を超えたため追加要求を無視します",
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
	ScreenSpaceOutlineViewResources& resources, std::span<const RenderPhase> phases,
	DepthTexture2D* depthOverride) {

	if (!resources.mask || !resources.projectedCoverageMask || phases.empty()) {
		return;
	}

	bool hasItems = false;
	for (RenderPhase phase : phases) {
		if (!passBuckets.Get(phase).IsEmpty()) {
			hasItems = true;
			break;
		}
	}
	if (!hasItems) {
		return;
	}

	const uint32_t prevStyleID = context.screenSpaceOutlineMaskStyleID;
	const int32_t prevSubMeshIndex = context.screenSpaceOutlineMaskRestrictSubMeshIndex;
	const uint32_t prevAlphaSource = context.screenSpaceOutlineMaskAlphaSource;

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	DxGPUEventScope eventScope{ commandList, L"SSOutline.MaskDraw" };

	// 同じstyleIDとsubMeshIndexのrecordをまとめて1パスで描く、複数選択でもパス数が増えない
	auto drawGroupedMask = [&](const RenderPassSurfaceBinding& binding, MaterialPassKind passKind, bool coverageOnly) {

		for (size_t groupBegin = 0; groupBegin < drawScratch_.size(); ) {

			const uint32_t groupStyleID = drawScratch_[groupBegin].styleID;
			const int32_t groupSubMeshIndex = drawScratch_[groupBegin].request.subMeshIndex;
			const ScreenSpaceOutlineAlphaSource groupAlphaSource =
				drawScratch_[groupBegin].request.alphaSource;
			const ScreenSpaceOutlineRegionMode groupRegion = drawScratch_[groupBegin].request.style.regionMode;

			size_t groupEnd = groupBegin;
			while (groupEnd < drawScratch_.size() &&
				drawScratch_[groupEnd].styleID == groupStyleID &&
				drawScratch_[groupEnd].request.subMeshIndex == groupSubMeshIndex &&
				drawScratch_[groupEnd].request.alphaSource == groupAlphaSource) {
				++groupEnd;
			}

			// CoverageはExteriorPreferredのstyleだけ描く、groupは同styleなので一括で判定できる
			if (!coverageOnly || groupRegion == ScreenSpaceOutlineRegionMode::ExteriorPreferred) {

				itemScratch_.clear();
				for (size_t recordIndex = groupBegin; recordIndex < groupEnd; ++recordIndex) {

					for (RenderPhase phase : phases) {

						const RenderPassItemList& list = passBuckets.Get(phase);
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
				}
				if (!itemScratch_.empty()) {

					context.screenSpaceOutlineMaskStyleID = groupStyleID;
					context.screenSpaceOutlineMaskRestrictSubMeshIndex = groupSubMeshIndex;
					context.screenSpaceOutlineMaskAlphaSource =
						static_cast<uint32_t>(groupAlphaSource);
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
		maskBinding.depthOverride = depthOverride;
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
	context.screenSpaceOutlineMaskAlphaSource = prevAlphaSource;
}
