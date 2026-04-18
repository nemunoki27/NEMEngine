#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackendTypes.h>

namespace Engine {

	//============================================================================
	//	IMeshDrawPath class
	//	メッシュ描画基底クラス
	//============================================================================
	class IMeshDrawPath {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IMeshDrawPath() = default;
		virtual ~IMeshDrawPath() = default;

		virtual bool Supports(const PipelineVariantDesc& variant) const = 0;

		// 経路固有の事前セットアップ
		virtual void Setup(const MeshPathSetupContext& context, std::vector<GraphicsBindItem>& scratch) = 0;

		// サブメッシュごとの描画
		virtual void Draw(const MeshPathDrawContext& context) = 0;
	};
} // Engine