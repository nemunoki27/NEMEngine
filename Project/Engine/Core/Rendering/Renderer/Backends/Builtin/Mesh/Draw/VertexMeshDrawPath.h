#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/ComPtr.h>

namespace Engine {

	//============================================================================
	//	VertexMeshDrawPath class
	//	頂点シェーダー描画パス
	//============================================================================
	class VertexMeshDrawPath :
		public IMeshDrawPath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		VertexMeshDrawPath() = default;
		~VertexMeshDrawPath() = default;

		bool Supports(const PipelineVariantDesc& variant) const override;

		void Setup(const MeshPathSetupContext& context, std::vector<GraphicsBindItem>& scratch) override;

		void Draw(const MeshPathDrawContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ExecuteIndirectでDrawIndexedInstancedを1回発行するためのシグネチャ
		ComPtr<ID3D12CommandSignature> commandSignature_{};
		// 可視インスタンスとIndirectArgsを生成するComputeパイプライン
		AssetID indirectArgsPipeline_{};

		//--------- functions ----------------------------------------------------

		void EnsureCommandSignature(ID3D12Device* device);
		// カリング結果を反映したDrawIndexedIndirect引数をGPU上で作成する
		bool BuildIndexedIndirectArgs(const MeshPathDrawContext& context);
	};
} // Engine
