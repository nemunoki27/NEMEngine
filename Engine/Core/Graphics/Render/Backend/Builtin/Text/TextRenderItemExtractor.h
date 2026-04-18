#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderItemExtractor.h>

namespace Engine {

	//============================================================================
	//	TextRenderItemExtractor class
	//	テキスト描画アイテム抽出器
	//============================================================================
	class TextRenderItemExtractor :
		public IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TextRenderItemExtractor() = default;
		~TextRenderItemExtractor() = default;

		void Extract(ECSWorld& world, RenderSceneBatch& batch) override;
	};
} // Engine