#include "RenderExtractorRegistry.h"

//============================================================================
//	RenderExtractorRegistry classMethods
//============================================================================

void Engine::RenderExtractorRegistry::Register(std::unique_ptr<IRenderItemExtractor> extractor) {

	if (!extractor) {
		return;
	}
	extractors_.emplace_back(std::move(extractor));
}

void Engine::RenderExtractorRegistry::BuildBatch(ECSWorld& world, RenderSceneBatch& batch) {

	// 全ての抽出器を呼び出して描画アイテムを抽出する
	batch.Clear();
	for (auto& extractor : extractors_) {

		extractor->Extract(world, batch);
	}
	// 描画アイテムをソートする
	batch.Sort();
}

void Engine::RenderExtractorRegistry::Clear() {

	extractors_.clear();
}