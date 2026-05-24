#include "RenderExtractorRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>

//============================================================================
//	RenderExtractorRegistry classMethods
//============================================================================

void Engine::RenderExtractorRegistry::BuildBatch(ECSWorld& world, RenderSceneBatch& batch) {

	// 全ての抽出器を呼び出して描画アイテムを抽出する
	batch.Clear();
	// 描画Entity数の上限をもとにフレーム中の再確保を抑える
	batch.Reserve(world.GetRecordCount(), world.GetRecordCount() * 128u);
	for (auto& extractor : items_) {

		extractor->Extract(world, batch);
	}
	// 描画アイテムをソートする
	batch.Sort();
}
