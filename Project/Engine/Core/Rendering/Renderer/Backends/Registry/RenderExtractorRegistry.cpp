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

	const uint64_t renderRevision = world.GetRenderDataRevision();
	const uint64_t transformRevision = world.GetRenderTransformRevision();
	if (batch.MatchesStructure(&world, renderRevision)) {

		if (batch.MatchesTransforms(transformRevision)) {
			return;
		}

		std::vector<Entity> changedEntities;
		const bool completeChanges =
			world.CollectRenderTransformChanges(
				batch.GetSourceTransformRevision(),
				changedEntities);
		if (completeChanges) {
			batch.RefreshTransforms(world, changedEntities);
		} else {
			// 履歴外の世代から再開した場合だけ安全側で全件更新する
			batch.RefreshAllTransforms();
		}
		batch.SetTransformSource(
			transformRevision, completeChanges);
		return;
	}

	// 全ての抽出器を呼び出して描画アイテムを抽出する
	batch.Clear();
	// 描画Entity数の上限をもとにフレーム中の再確保を抑える
	batch.Reserve(world.GetRecordCount(), world.GetRecordCount() * 128u);
	for (auto& extractor : items_) {

		extractor->Extract(world, batch);
	}
	// 描画アイテムをソートする
	batch.Sort();
	batch.SetSource(&world, renderRevision, transformRevision);
}
