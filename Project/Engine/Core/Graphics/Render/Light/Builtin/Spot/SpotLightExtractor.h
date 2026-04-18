#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/Interface/ILightExtractor.h>

namespace Engine {

	//============================================================================
	//	SpotLightExtractor class
	//	スポットライトを抽出するクラス
	//============================================================================
	class SpotLightExtractor :
		public ILightExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SpotLightExtractor() = default;
		~SpotLightExtractor() override = default;

		void Extract(ECSWorld& world, FrameLightBatch& batch) override;
	};
} // Engine