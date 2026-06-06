#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	//============================================================================
	//	MeshShaderDrawPath class
	//	メッシュシェーダー描画パス
	//============================================================================
	class MeshShaderDrawPath :
		public IMeshDrawPath {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		MeshShaderDrawPath();
		~MeshShaderDrawPath() = default;

		bool Supports(const PipelineVariantDesc& variant) const override;

		void Setup(const MeshPathSetupContext& context) override;

		void Draw(const MeshPathDrawContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- variables ----------------------------------------------------

		// メッシュレット関連SRVのパイプライン解決キャッシュ
		PipelineBindingCache meshletBindCache_;
		PipelineBindingCache::SlotID indicesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshletsSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshletBoundsSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshletVtxIdxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID meshletPrimIdxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID pkdMeshletVtxIdxSRVSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine