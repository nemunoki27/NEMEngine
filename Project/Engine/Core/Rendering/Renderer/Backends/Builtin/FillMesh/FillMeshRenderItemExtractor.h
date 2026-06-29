#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	FillMeshRenderItemExtractor class
	//	面メッシュ描画アイテム抽出器
	//============================================================================
	class FillMeshRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FillMeshRenderItemExtractor() = default;
		~FillMeshRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine
