#include "PointLightExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Light/PointLightComponent.h>

//============================================================================
//	PointLightExtractor classMethods
//============================================================================

void Engine::PointLightExtractor::Extract(ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<PointLightComponent>([&](const Entity& entity, PointLightComponent& light) {

		// 抽出可能か
		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		// ライトアイテムを作成してバッチに追加
		PointLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.pos = LightExtract::GetWorldPos(world, entity);
		item.intensity = light.intensity;
		item.radius = light.radius;
		item.decay = light.decay;
		batch.Add(std::move(item));
		});
}