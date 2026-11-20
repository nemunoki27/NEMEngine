#include "UVTransformSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

//============================================================================
//	UVTransformSystem classMethods
//============================================================================

void Engine::UVTransformSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	world.ForEach<UVTransformComponent>([&](Entity entity, UVTransformComponent& uvTransform) {

		if (!IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		// 変更があったかどうかを判定
		if (uvTransform.pos != uvTransform.prePos ||
			uvTransform.rotation != uvTransform.preRotation ||
			uvTransform.scale != uvTransform.preScale) {

			uvTransform.uvMatrix = MakeUVMatrix(uvTransform);
			world.MarkComponentModified<UVTransformComponent>(entity);
		}

		// 前フレームのSRTを更新
		uvTransform.prePos = uvTransform.pos;
		uvTransform.preRotation = uvTransform.rotation;
		uvTransform.preScale = uvTransform.scale;
		});
}
