#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>

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
	};
} // Engine