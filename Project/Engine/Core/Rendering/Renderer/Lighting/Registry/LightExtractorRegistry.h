#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/Foundation/Utility/Registry/RegistryBase.h>

namespace Engine {

	//============================================================================
	//	LightExtractorRegistry class
	//	ライト抽出器レジストリ
	//============================================================================
	class LightExtractorRegistry :
		public ListRegistryBase<ILightExtractor> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LightExtractorRegistry() = default;
		~LightExtractorRegistry() override = default;

		// ライト抽出器を呼び出してバッチを構築する
		void BuildBatch(ECSWorld& world, FrameLightBatch& batch);
	};
} // Engine