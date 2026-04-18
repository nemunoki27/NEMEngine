#include "SpotLightExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Light/SpotLightComponent.h>

//============================================================================
//	SpotLightExtractor classMethods
//============================================================================

void Engine::SpotLightExtractor::Extract(ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<SpotLightComponent>([&](const Entity& entity, SpotLightComponent& light) {

		// 抽出可能か
		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		// ワールド行列を取得
		Matrix4x4 worldMatrix = LightExtract::GetWorldMatrix(world, entity);

		// ライトアイテムを作成してバッチに追加
		SpotLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.pos = worldMatrix.GetTranslationValue();
		item.direction = LightExtract::GetWorldDirection(light.direction, worldMatrix);
		item.intensity = light.intensity;
		item.distance = light.distance;
		item.decay = light.decay;
		item.cosAngle = light.cosAngle;
		item.cosFalloffStart = light.cosFalloffStart;
		batch.Add(std::move(item));
		});
}