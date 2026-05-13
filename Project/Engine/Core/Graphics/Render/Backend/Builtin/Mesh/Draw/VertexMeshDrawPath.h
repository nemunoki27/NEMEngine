#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

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

		ComPtr<ID3D12CommandSignature> commandSignature_{};

		//--------- functions ----------------------------------------------------

		void EnsureCommandSignature(ID3D12Device* device);
	};
} // Engine
