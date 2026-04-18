#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Pass/RenderItemBatchDispatcher.h>
#include <Engine/Core/Graphics/Render/Queue/RenderPassItemCollector.h>
#include <Engine/Core/Graphics/Render/Texture/RenderTargetRegistry.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Pipeline/PipelineStateCache.h>
#include <Engine/Core/Graphics/Material/MaterialResolver.h>
#include <Engine/Core/Graphics/Render/Backend/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Graphics/Render/View/RenderFrameTypes.h>
#include <Engine/Core/Graphics/Render/View/ViewportRenderService.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingPipelineStateCache.h>

namespace Engine {

	//============================================================================
	//	ScenePassExecutor class
	//	シーンの描画パスを実行するクラス
	//============================================================================
	class ScenePassExecutor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 依存関係の構造体
		struct Dependencies {

			RenderSceneBatch* renderBatch = nullptr;
			RenderBackendRegistry* backendRegistry = nullptr;
			RenderAssetLibrary* assetLibrary = nullptr;
			PipelineStateCache* pipelineCache = nullptr;
			MaterialResolver* materialResolver = nullptr;
			RaytracingPipelineStateCache* raytracingPipelineCache = nullptr;
			const ViewportRenderService* viewportService = nullptr;
			const RenderItemBatchDispatcher* dispatcher = nullptr;
		};
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScenePassExecutor(Dependencies deps) : dependencies(deps) {}
		~ScenePassExecutor() = default;

		// シーン描画の実行
		void ExecuteScene(GraphicsCore& graphicsCore, const RenderFrameRequest& request, const SceneExecutionContext& context);

		// フルスクリーンマテリアルの実行
		bool ExecuteFullscreenMaterial(GraphicsCore& graphicsCore, const SceneExecutionContext& context, MultiRenderTarget* source,
			MultiRenderTarget* dest, AssetID requestedMaterial, DefaultMaterialSlot defaultSlot, const std::string_view& passName) const;

		// すべてのターゲットをシェーダーリード用に遷移
		void TransitionAllTargetsToShaderRead(GraphicsCore& graphicsCore, const SceneExecutionContext& context) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Dependencies dependencies{};

		//--------- functions ----------------------------------------------------

		// レンダーターゲットをクリア
		void ExecuteClearPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const ClearPassDesc& pass) const;
		// 深度プリパスの実行
		void ExecuteDepthPrepassPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
			const DepthPrepassPassDesc& pass, const RenderPassPhaseBuckets& passBuckets) const;
		// バッチングされたアイテムを描画
		void ExecuteDrawPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
			const DrawPassDesc& pass, const RenderPassPhaseBuckets& passBuckets) const;
		// ポストプロセスパスの実行
		void ExecutePostProcessPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const PostProcessPassDesc& pass) const;
		// コンピュートパスの実行
		void ExecuteComputePass(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const ComputePassDesc& pass) const;
		// ブリットパスの実行
		void ExecuteBlitPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const BlitPassDesc& pass) const;
		// レイトレーシングパスの実行
		void ExecuteRaytracingPass(GraphicsCore& graphicsCore, const SceneExecutionContext& context, const RaytracingPassDesc& pass) const;
		// レンダーシーンパスの実行
		void ExecuteRenderScenePass(GraphicsCore& graphicsCore, const RenderFrameRequest& request,
			const SceneExecutionContext& context, const RenderScenePassDesc& pass);

		// レンダーターゲットの適用
		MultiRenderTarget* ApplyRenderTargets(GraphicsCore& graphicsCore,
			const SceneExecutionContext& context, const RenderTargetSetReference& dest) const;

		// 深度プリパス用の描画アイテムを収集
		std::vector<const RenderItem*> CollectDepthPrepassItems(const SceneExecutionContext& context,
			const DepthPrepassPassDesc& pass, const RenderPassPhaseBuckets& passBuckets) const;
		// デフォルトサーフェスへの明示的なクリアが必要か
		bool HasExplicitClearForDefaultSurface(const SceneExecutionContext& context, const SceneHeader& header) const;
	};
} // Engine