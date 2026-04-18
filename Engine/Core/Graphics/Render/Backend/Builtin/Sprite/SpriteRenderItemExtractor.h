#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	SpriteRenderItemExtractor class
	//	スプライト描画アイテム抽出器
	//============================================================================
	class SpriteRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SpriteRenderItemExtractor() = default;
		~SpriteRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine