#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/BuiltinLightExtractors.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>

// c++
#include <memory>

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================
using namespace Engine;

void RenderPipelineRunner::Init() {

	viewportRenderService_ = std::make_unique<ViewportRenderService>();

	// 描画アイテム抽出器の登録
	extractorRegistry_.Clear();
	extractorRegistry_.Register(std::make_unique<SpriteRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<TextRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<MeshRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<LineRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<PrimitiveRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<ParticleRenderItemExtractor>());
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	backendRegistry_.Register(std::make_unique<LineRenderBackend>());
	backendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	backendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewResources_.previewBackendRegistry_.Clear();
	previewResources_.previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<LineRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// 型付きMeshバックエンドをキャッシュして毎フレームのdynamic_castを避ける
	meshBackend_ = dynamic_cast<MeshRenderBackend*>(backendRegistry_.Find(RenderBackendID::Mesh));
	previewResources_.previewMeshBackend_ =
		dynamic_cast<MeshRenderBackend*>(previewResources_.previewBackendRegistry_.Find(RenderBackendID::Mesh));
	previewResources_.previewMeshBackend_->SetUploadLimit(1);
	primitiveBackend_ = dynamic_cast<PrimitiveRenderBackend*>(backendRegistry_.Find(RenderBackendID::Primitive));
	particleBackend_ = dynamic_cast<ParticleRenderBackend*>(backendRegistry_.Find(RenderBackendID::Particle));
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<RectLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	postProcessExecutor_.Release();
	rayTracingExecutor_.Release();
	colorPipelineProcessor_.Release();
	postProcessAssetGenerator_.Clear();
	scenePreparation_.frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewResources_.previewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();

	// 固定RenderPathの初期化
	{
		RenderPipelineDeps deps{};
		deps.renderBatch = &scenePreparation_.renderBatch_;
		deps.backendRegistry = &backendRegistry_;
		deps.assetLibrary = &renderAssetLibrary_;
		deps.pipelineCache = &pipelineStateCache_;
		deps.materialResolver = &materialResolver_;
		deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
		deps.rayTracingExecutor = &rayTracingExecutor_;
		deps.postProcessExecutor = &postProcessExecutor_;
		deps.postProcessTargetPool = &postProcessTargetPool_;
		deps.postProcessDebugInjector = &postProcessDebugInjector_;
		deps.postProcessAssetGenerator = &postProcessAssetGenerator_;
		deps.colorPipelineProcessor = &colorPipelineProcessor_;
		deps.dispatcher = &batchDispatcher_;
		renderPath_.Initialize(deps);
	}

	cameraStates_.clear();
	cameraWorldLifetime_.reset();

	// ビューライトバッファの初期化
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewResources_.previewLightBufferPool_.Clear();
	previewResources_.previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
}

void RenderPipelineRunner::Finalize() {

	// Camera別のGPU資源をDevice終了前に回収する
	cameraStates_.clear();
	cameraWorldLifetime_.reset();
	// GPU計測の資源をDevice終了前に解放する
	GPUFrameProfiler::GetInstance().Finalize();
	// Worldや登録処理より先に抽出結果の借用を解除する
	scenePreparation_.renderBatch_.Clear();
	previewResources_.previewScenePreparation_.renderBatch_.Clear();
	previewResources_.previewScenePreparation_.frameLightBatch_.Clear();

	renderPath_.Finalize();
	backendRegistry_.Clear();
	previewResources_.previewBackendRegistry_.Clear();
	meshBackend_ = nullptr;
	primitiveBackend_ = nullptr;
	particleBackend_ = nullptr;
	previewResources_.previewMeshBackend_ = nullptr;
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	postProcessExecutor_.Release();
	rayTracingExecutor_.Release();
	colorPipelineProcessor_.Release();
	postProcessAssetGenerator_.Clear();
	lightExtractorRegistry_.Clear();
	scenePreparation_.frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewResources_.previewLightSet_.Clear();
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewResources_.previewLightBufferPool_.Clear();
	assetReloadService_.ClearRenderStates();
	if (viewportRenderService_) {
		viewportRenderService_->Finalize();
		viewportRenderService_.reset();
	}
	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();
	previewResources_.previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
	pickingState_.lastRenderRequest_ = {};
	pickingState_.lastActiveScene_ = nullptr;
	raytracingSceneBuilder_.Finalize();
	gameViewState_.resources.Destroy();
	sceneViewState_.resources.Destroy();
}
