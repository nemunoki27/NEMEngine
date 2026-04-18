#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/Interface/ILightExtractor.h>

namespace Engine {

	//============================================================================
	//	PointLightExtractor class
	//	ポイントライトを抽出するクラス
	//============================================================================
	class PointLightExtractor :
		public ILightExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PointLightExtractor() = default;
		~PointLightExtractor() override = default;

		void Extract(ECSWorld& world, FrameLightBatch& batch) override;
	};
} // Engine