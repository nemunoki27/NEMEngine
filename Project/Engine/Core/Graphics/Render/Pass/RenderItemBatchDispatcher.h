#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/Backend/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderBackend.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Pipeline/PipelineStateCache.h>
#include <Engine/Core/Graphics/Material/MaterialResolver.h>

namespace Engine {

	// front
	struct SceneExecutionContext;

	//============================================================================
	//	RenderItemBatchDispatcher class
	//	描画アイテムのバッチングを行うクラス
	//============================================================================
	class RenderItemBatchDispatcher {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderItemBatchDispatcher() = default;
		~RenderItemBatchDispatcher() = default;

		// 描画アイテムのバッチング
		void Dispatch(GraphicsCore& graphicsCore, const SceneExecutionContext& sceneContext, const RenderSceneBatch& renderBatch,
			RenderBackendRegistry& backendRegistry, RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
			MaterialResolver& materialResolver, const std::vector<const RenderItem*>& items, const MultiRenderTarget* surface,
			const std::string_view& passName, bool depthOnly) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 描画フォーマットを取得
		static void FillColorFormats(const MultiRenderTarget* surface,
			std::array<DXGI_FORMAT, 8>& outFormats, uint32_t& outCount);
		static DXGI_FORMAT GatherDepthFormat(const MultiRenderTarget* surface);
	};
} // Engine