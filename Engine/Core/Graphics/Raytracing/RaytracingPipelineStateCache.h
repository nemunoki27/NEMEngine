#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsPlatform.h>
#include <Engine/Core/Graphics/Asset/RenderAssetLibrary.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingPipelineState.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RaytracingPipelineStateCache class
	//	レイトレーシングパイプラインステートのキャッシュを管理するクラス
	//============================================================================
	class RaytracingPipelineStateCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RaytracingPipelineStateCache() = default;
		~RaytracingPipelineStateCache() = default;

		// パイプラインステートの取得、キャッシュに存在しない場合は作成してキャッシュする
		RaytracingPipelineState* GetOrCreate(GraphicsPlatform& graphicsPlatform,
			RenderAssetLibrary& assetLibrary, AssetID pipelineAssetID);

		// データクリア
		void Clear() { cache_.clear(); }

		//--------- accessor -----------------------------------------------------

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<AssetID, std::unique_ptr<RaytracingPipelineState>> cache_{};
	};
} // Engine