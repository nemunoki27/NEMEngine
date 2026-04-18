#include "UVTransformUpdateSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/UVTransformComponent.h>

//============================================================================
//	UVTransformUpdateSystem classMethods
//============================================================================

void Engine::UVTransformUpdateSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	world.ForEach<UVTransformComponent>([&]([[maybe_unused]] Entity entity, UVTransformComponent& uvTransform) {

		// 変更があったかどうかを判定
		if (uvTransform.pos != uvTransform.prePos ||
			uvTransform.rotation != uvTransform.preRotation ||
			uvTransform.scale != uvTransform.preScale) {

			// SRTを3次元ベクトルに変換
			Vector3 scale = Vector3(uvTransform.scale.x, uvTransform.scale.y, 1.0f);
			Vector3 rotation = Vector3(0.0f, 0.0f, uvTransform.rotation);
			Vector3 pos = Vector3(uvTransform.pos.x, uvTransform.pos.y, 0.0f);

			// 行列を計算
			uvTransform.uvMatrix = Matrix4x4::MakeAffineMatrix(scale, rotation, pos);
		}

		// 前フレームのSRTを更新
		uvTransform.prePos = uvTransform.pos;
		uvTransform.preRotation = uvTransform.rotation;
		uvTransform.preScale = uvTransform.scale;
		});
}