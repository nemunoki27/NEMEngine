#include "LightExtractorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>

//============================================================================
//	LightExtractorRegistry classMethods
//============================================================================

void Engine::LightExtractorRegistry::BuildBatch(ECSWorld& world, FrameLightBatch& batch) {

	// ライト抽出器を呼び出してバッチを構築する
	batch.Clear();
	for (auto& extractor : items_) {

		extractor->Extract(world, batch);
	}
	batch.Sort();
}