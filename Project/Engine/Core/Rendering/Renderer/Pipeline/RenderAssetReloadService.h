#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <unordered_map>

namespace Engine {

	class AssetDatabase;
	class RenderSceneBatch;
	class RenderAssetLibrary;
	class PipelineStateCache;
	class RaytracingPipelineStateCache;
	class PostProcessExecutor;
	class RayTracingExecutor;

	//============================================================================
	//	RenderAssetReloadService class
	//	描画Assetの再読込順とCache失効を管理する
	//============================================================================
	class RenderAssetReloadService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderAssetReloadService(RenderAssetLibrary& renderAssetLibrary,
			PipelineStateCache& pipelineStateCache,
			RaytracingPipelineStateCache& raytracingPipelineStateCache,
			PostProcessExecutor& postProcessExecutor,
			RayTracingExecutor& rayTracingExecutor);

		void ReloadMaterial(AssetID materialAssetID);
		void ReloadShader(AssetID shaderAssetID);
		void ReloadPipeline(AssetID pipelineAssetID);
		bool ReloadMaterialDependencies(AssetDatabase& assetDatabase,
			AssetID materialAssetID);
		void ReloadAsset(AssetDatabase& assetDatabase, AssetID assetID);
		// Materialの描画設定を抽出結果へ反映する
		void ApplyMaterialRenderStates(RenderSceneBatch& renderBatch);
		// 描画状態のcacheを破棄する
		void ClearRenderStates();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		RenderAssetLibrary& renderAssetLibrary_;
		PipelineStateCache& pipelineStateCache_;
		RaytracingPipelineStateCache& raytracingPipelineStateCache_;
		PostProcessExecutor& postProcessExecutor_;
		RayTracingExecutor& rayTracingExecutor_;
		std::unordered_map<AssetID, MaterialRenderState> materialRenderStateCache_{};
	};
}
