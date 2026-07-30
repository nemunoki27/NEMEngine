#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingPlatform.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RaytracingPipelineCacheKey structure
	//============================================================================
	struct RaytracingPipelineCacheKey {

		AssetID pipelineAsset{};
		AssetID pipelineShaderAsset{};
		AssetID shaderOverrideAsset{};

		bool operator==(const RaytracingPipelineCacheKey& rhs) const noexcept;
	};

	//============================================================================
	//	RaytracingPipelineStateCache class
	//	レイトレーシングパイプラインステートのキャッシュを管理するクラス
	//============================================================================
	class RaytracingPipelineStateCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RaytracingPipelineStateCache() = default;
		~RaytracingPipelineStateCache() = default;

		// パイプラインステートの取得、キャッシュに存在しない場合は作成してキャッシュする
		RaytracingPipelineState* GetOrCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID,
			AssetID shaderOverrideAssetID = {});

		// データクリア
		void Clear();
		// 指定Pipelineから生成したState Objectだけを破棄する
		void InvalidateByPipelineAsset(AssetID pipelineAssetID);

		//--------- accessor -----------------------------------------------------

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		struct RaytracingPipelineCacheKeyHash {
			size_t operator()(const RaytracingPipelineCacheKey& key) const noexcept {

				size_t hash = std::hash<AssetID>{}(key.pipelineAsset);
				hash ^= std::hash<AssetID>{}(key.pipelineShaderAsset) << 1;
				hash ^= std::hash<AssetID>{}(key.shaderOverrideAsset) << 2;
				return hash;
			}
		};

		std::unordered_map<RaytracingPipelineCacheKey,
			std::unique_ptr<RaytracingPipelineState>,
			RaytracingPipelineCacheKeyHash> cache_{};
	};
} // Engine

