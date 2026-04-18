#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/Draw/Interface/IMeshDrawPath.h>

namespace Engine {

	//============================================================================
	//	MeshShaderDrawPath class
	//	メッシュシェーダー描画パス
	//============================================================================
	class MeshShaderDrawPath :
		public IMeshDrawPath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshShaderDrawPath() = default;
		~MeshShaderDrawPath() = default;

		bool Supports(const PipelineVariantDesc& variant) const override;

		void Setup(const MeshPathSetupContext& context, std::vector<GraphicsBindItem>& scratch) override;

		void Draw(const MeshPathDrawContext& context) override;
	};
} // Engine