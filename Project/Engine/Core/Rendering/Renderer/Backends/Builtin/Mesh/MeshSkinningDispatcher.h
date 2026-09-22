#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	struct RenderDrawContext;
	struct MeshPreparedBatch;
	//============================================================================
	//	MeshSkinningDispatcher class
	//	Skinningに必要な資源を接続して命令を発行する
	//============================================================================
	class MeshSkinningDispatcher {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshSkinningDispatcher();
		// SkinningのGPU命令を発行して状態を更新する
		void Dispatch(const RenderDrawContext& context, const MeshPreparedBatch& prepared);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		PipelineBindingCache skinningBindCache_{};
		PipelineBindingCache::SlotID skinConstCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID inputVtxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID vtxInflSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinPaletteSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedVtxUAVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID skinnedPkdVtxUAVSlot_ = PipelineBindingCache::kInvalidSlot;

		AssetID skinningPipeline_{};
	};
}
