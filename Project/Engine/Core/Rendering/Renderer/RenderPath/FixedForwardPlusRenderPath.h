#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessDebugInjector.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessTemporaryTargetPool.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPassPhaseBuckets;
	class RenderSceneBatch;

	//============================================================================
	//	RenderPipelineDeps structure
	//	FixedForwardPlusRenderPathおよびIRenderPass実装が使うパイプライン依存
	//============================================================================
	struct RenderPipelineDeps {

		RenderSceneBatch* renderBatch = nullptr;
		RenderBackendRegistry* backendRegistry = nullptr;
		RenderAssetLibrary* assetLibrary = nullptr;
		PipelineStateCache* pipelineCache = nullptr;
		MaterialResolver* materialResolver = nullptr;
		RaytracingPipelineStateCache* raytracingPipelineCache = nullptr;
		PostProcessExecutor* postProcessExecutor = nullptr;
		PostProcessTemporaryTargetPool* postProcessTargetPool = nullptr;
		PostProcessDebugInjector* postProcessDebugInjector = nullptr;
		PostProcessAssetGenerator* postProcessAssetGenerator = nullptr;
		const RenderItemBatchDispatcher* dispatcher = nullptr;
	};

	//============================================================================
	//	FixedForwardPlusRenderPath class
	//	固定描画順のForward+ RenderPathを管理・実行するクラス
	//============================================================================
	class FixedForwardPlusRenderPath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FixedForwardPlusRenderPath() = default;
		~FixedForwardPlusRenderPath() = default;

		// コピー禁止
		FixedForwardPlusRenderPath(const FixedForwardPlusRenderPath&) = delete;
		FixedForwardPlusRenderPath& operator=(const FixedForwardPlusRenderPath&) = delete;

		// パスの初期化。deps は RenderPipelineRunner が所有するメンバーへのポインタを渡す。
		void Initialize(const RenderPipelineDeps& deps);
		// 終了処理
		void Finalize();

		// 1ビュー分の固定描画順を実行する
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		RenderPipelineDeps deps_{};
		std::vector<std::unique_ptr<IRenderPass>> passes_{};
	};
} // Engine
