#include "DirectionalLightExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/Light/DirectionalLightComponent.h>

//============================================================================
//	DirectionalLightExtractor classMethods
//============================================================================

void Engine::DirectionalLightExtractor::Extract(ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<DirectionalLightComponent>([&](const Entity& entity, DirectionalLightComponent& light) {

		// 抽出可能か
		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		// ワールド行列を取得
		Matrix4x4 worldMatrix = LightExtract::GetWorldMatrix(world, entity);

		// ライトアイテムを作成してバッチに追加
		DirectionalLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.intensity = light.intensity;
		item.direction = LightExtract::GetWorldDirection(light.direction, worldMatrix);
		batch.Add(std::move(item));
		});
}