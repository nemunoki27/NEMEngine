#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Interface/IRenderItemExtractor.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderExtractorRegistry class
	//	描画アイテム抽出器のレジストリ
	//============================================================================
	class RenderExtractorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderExtractorRegistry() = default;
		~RenderExtractorRegistry() = default;

		// 登録
		void Register(std::unique_ptr<IRenderItemExtractor> extractor);
		// 描画アイテムの抽出
		void BuildBatch(ECSWorld& world, RenderSceneBatch& batch);

		// データクリア
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 描画アイテム抽出器のリスト
		std::vector<std::unique_ptr<IRenderItemExtractor>> extractors_;
	};
} // Engine