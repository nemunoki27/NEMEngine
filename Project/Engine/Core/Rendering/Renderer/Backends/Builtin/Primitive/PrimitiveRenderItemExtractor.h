#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	PrimitiveRenderItemExtractor class
	//	プロシージャル形状描画アイテム抽出器
	//============================================================================
	class PrimitiveRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrimitiveRenderItemExtractor() = default;
		~PrimitiveRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine
