#include "RenderItemBatchDispatcher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/View/RenderViewResolver.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>

//============================================================================
//	RenderItemBatchDispatcher classMethods
//============================================================================

void Engine::RenderItemBatchDispatcher::Dispatch(GraphicsCore& graphicsCore, const SceneExecutionContext& sceneContext,
	const RenderSceneBatch& renderBatch, RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, MaterialResolver& materialResolver, const std::vector<const RenderItem*>& items,
	const MultiRenderTarget* surface, const std::string_view& passName, bool depthOnly) const {

	// 描画コンテキストの構築
	RenderDrawContext drawContext{};
	drawContext.graphicsCore = &graphicsCore;
	drawContext.view = sceneContext.view;
	drawContext.systemContext = sceneContext.systemContext;
	drawContext.batch = &renderBatch;
	drawContext.bufferRegistry = &sceneContext.bufferRegistry;
	drawContext.assetDatabase = sceneContext.assetDatabase;
	drawContext.assetLibrary = &assetLibrary;
	drawContext.pipelineCache = &pipelineCache;
	drawContext.materialResolver = &materialResolver;
	drawContext.passName = passName;
	drawContext.depthOnly = depthOnly;

	drawContext.rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
	drawContext.numRTVFormats = 0;

	// 深度描画のみを行うか
	if (depthOnly) {
		drawContext.dsvFormat = (surface && surface->GetDepthTexture()) ?
			surface->GetDepthTexture()->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
	} else {
		FillColorFormats(surface, drawContext.rtvFormats, drawContext.numRTVFormats);
		drawContext.dsvFormat = GatherDepthFormat(surface);
	}

	// GPUのランタイム機能を取得
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();

	size_t begin = 0;
	while (begin < items.size()) {

		const RenderItem* first = items[begin];
		if (!first) {
			++begin;
			continue;
		}
		IRenderBackend* backend = backendRegistry.Find(first->backendID);
		if (!backend) {
			++begin;
			continue;
		}

		// 描画アイテムをバッチングできる限りまとめる
		size_t end = begin + 1;
		while (end < items.size()) {
			const RenderItem* next = items[end];
			if (!next) {
				break;
			}

			if (next->backendID != first->backendID ||
				!backend->CanBatch(*first, *next, runtimeFeatures)) {
				break;
			}
			++end;
		}
		// バッチングしたアイテムを描画
		backend->DrawBatch(drawContext, std::span(items.data() + begin, end - begin));
		// 次のアイテムへ
		begin = end;
	}
}

void Engine::RenderItemBatchDispatcher::FillColorFormats(const MultiRenderTarget* surface,
	std::array<DXGI_FORMAT, 8>& outFormats, uint32_t& outCount) {

	outFormats.fill(DXGI_FORMAT_UNKNOWN);
	outCount = 0;
	if (!surface) {
		return;
	}
	const uint32_t colorCount = (std::min)(surface->GetColorCount(), static_cast<uint32_t>(outFormats.size()));
	for (uint32_t i = 0; i < colorCount; ++i) {
		if (auto* color = surface->GetColorTexture(i)) {

			outFormats[outCount++] = color->GetFormat();
		}
	}
}

DXGI_FORMAT Engine::RenderItemBatchDispatcher::GatherDepthFormat(const MultiRenderTarget* surface) {

	if (!surface) {
		return DXGI_FORMAT_UNKNOWN;
	}
	if (auto* depth = surface->GetDepthTexture()) {
		return depth->GetDSVFormat();
	}
	return DXGI_FORMAT_UNKNOWN;
}
