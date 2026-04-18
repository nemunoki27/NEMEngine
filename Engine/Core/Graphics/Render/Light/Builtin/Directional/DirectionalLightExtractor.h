#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/Interface/ILightExtractor.h>

namespace Engine {

	//============================================================================
	//	DirectionalLightExtractor class
	//	平行光源を抽出するクラス
	//============================================================================
	class DirectionalLightExtractor :
		public ILightExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DirectionalLightExtractor() = default;
		~DirectionalLightExtractor() override = default;

		void Extract(ECSWorld& world, FrameLightBatch& batch) override;
	};
} // Engine