#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Light/Interface/ILightExtractor.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	LightExtractorRegistry class
	//	ライト抽出器レジストリ
	//============================================================================
	class LightExtractorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LightExtractorRegistry() = default;
		~LightExtractorRegistry() = default;

		// ライト抽出器の登録
		void Register(std::unique_ptr<ILightExtractor> extractor);

		// ライト抽出器を呼び出してバッチを構築する
		void BuildBatch(ECSWorld& world, FrameLightBatch& batch);

		// データクリア
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::unique_ptr<ILightExtractor>> extractors_;
	};
} // Engine