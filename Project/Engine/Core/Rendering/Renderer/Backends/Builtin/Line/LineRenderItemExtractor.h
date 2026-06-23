#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	LineRenderItemExtractor class
	//	ライン描画アイテム抽出器
	//============================================================================
	class LineRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineRenderItemExtractor() = default;
		~LineRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine
