#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Foundation/Utility/Registry/RegistryBase.h>

namespace Engine {

	class ECSWorld;
	class RenderSceneBatch;

	//============================================================================
	//	RenderExtractorRegistry class
	//	描画アイテム抽出器のレジストリ
	//============================================================================
	class RenderExtractorRegistry :
		public ListRegistryBase<IRenderItemExtractor> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenderExtractorRegistry() = default;
		~RenderExtractorRegistry() override = default;

		// 描画アイテムの抽出
		void BuildBatch(ECSWorld& world, RenderSceneBatch& batch);
	private:
		// 外部データの世代を登録順に保持する
		std::vector<uint64_t> contentRevisions_;
		// Transform差分の収集領域をフレーム間で再利用する
		std::vector<Entity> changedEntities_;
	};
} // Engine
