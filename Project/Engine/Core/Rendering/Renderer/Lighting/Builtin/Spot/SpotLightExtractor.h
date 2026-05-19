#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>

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