#include "LightExtractorRegistry.h"

//============================================================================
//	LightExtractorRegistry classMethods
//============================================================================

void Engine::LightExtractorRegistry::Register(std::unique_ptr<ILightExtractor> extractor) {

	if (!extractor) {
		return;
	}
	extractors_.emplace_back(std::move(extractor));
}

void Engine::LightExtractorRegistry::BuildBatch(ECSWorld& world, FrameLightBatch& batch) {

	// ライト抽出器を呼び出してバッチを構築する
	batch.Clear();
	for (auto& extractor : extractors_) {

		extractor->Extract(world, batch);
	}
	batch.Sort();
}

void Engine::LightExtractorRegistry::Clear() {

	extractors_.clear();
}