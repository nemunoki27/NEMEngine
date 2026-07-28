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

		// Transform変更だけなら既存アイテムの行列を更新し、
		// コンポーネント抽出、ペイロード構築、ソートを省略する
		for (RenderItem& item : batch.GetMutableItems()) {
			if (!item.world || !item.world->IsAlive(item.entity)) {
				continue;
			}
			item.worldMatrix =
				RenderItemExtract::GetWorldMatrix(*item.world, item.entity);
		}
		batch.SetTransformSource(transformRevision);
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
