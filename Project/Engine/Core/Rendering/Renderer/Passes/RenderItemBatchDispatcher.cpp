#include "RenderItemBatchDispatcher.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	RenderItemBatchDispatcher classMethods
//============================================================================
void Engine::RenderItemBatchDispatcher::Dispatch(GraphicsCore& graphicsCore, const SceneExecutionContext& sceneContext,
	const RenderSceneBatch& renderBatch, RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, MaterialResolver& materialResolver, const std::vector<const RenderItem*>& items,
	const MultiRenderTarget* surface, const DepthTexture2D* depthOverride, MaterialPassKind passKind, bool depthOnly) const {

	// 描画コンテキストの構築
	RenderDrawContext drawContext{};
	drawContext.graphicsCore = &graphicsCore;
	drawContext.view = sceneContext.view;
	// SceneView描画時でもGameViewカメラでカリングできるように別ポインタで渡す
	drawContext.cullingView = sceneContext.cullingView;
	// ビルボード計算も同様にGameViewを優先するための参照を渡す
	drawContext.billboardView = sceneContext.billboardView;
	drawContext.systemContext = sceneContext.systemContext;
	drawContext.batch = &renderBatch;
	// View共通リソースを各Backendへ渡す
	drawContext.bufferRegistry = &sceneContext.bufferRegistry;
	drawContext.assetDatabase = sceneContext.assetDatabase;
	drawContext.assetLibrary = &assetLibrary;
	drawContext.pipelineCache = &pipelineCache;
	drawContext.materialResolver = &materialResolver;
	drawContext.passKind = passKind;
	drawContext.depthOnly = depthOnly;
	drawContext.forceVertexMeshVariant = sceneContext.forceVertexMeshVariant;
	// ScreenSpaceOutline Mask描画のper-draw値を引き継ぐ
	drawContext.screenSpaceOutlineMaskStyleID = sceneContext.screenSpaceOutlineMaskStyleID;
	drawContext.screenSpaceOutlineMaskRestrictSubMeshIndex = sceneContext.screenSpaceOutlineMaskRestrictSubMeshIndex;

	// プレビューではTLASを作らないため、RayQueryを要求するVariantだけ外して解決する
	drawContext.runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	if (sceneContext.disableInlineRayTracing) {

		drawContext.runtimeFeatures.useInlineRayTracing = false;
		drawContext.runtimeFeatures.useDispatchRays = false;
	}
	if (sceneContext.forceDirectLocalLightEvaluation) {

		drawContext.runtimeFeatures.useLightCulling = false;
		drawContext.runtimeFeatures.lightCullingMode = LightCullingMode::Disabled;
	}

	drawContext.rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
	drawContext.numRTVFormats = 0;

	// 外部DSV指定があればそちらを優先して深度フォーマットを解決する
	const DepthTexture2D* boundDepth = depthOverride
		? depthOverride
		: (surface ? surface->GetDepthTexture() : nullptr);

	// 深度描画のみを行うか
	if (depthOnly) {
		drawContext.dsvFormat = boundDepth ? boundDepth->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
	} else {
		FillColorFormats(surface, drawContext.rtvFormats, drawContext.numRTVFormats);
		drawContext.dsvFormat = boundDepth ? boundDepth->GetDSVFormat() : DXGI_FORMAT_UNKNOWN;
	}

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
				!backend->CanBatch(*first, *next, drawContext.runtimeFeatures)) {
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
