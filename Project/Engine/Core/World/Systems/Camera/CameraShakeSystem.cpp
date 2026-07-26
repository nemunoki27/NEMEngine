#include "CameraShakeSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraShakeComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Utility/Random/RandomGenerator.h>

//============================================================================
//	CameraShakeSystem classMethods
//============================================================================

void CameraShakeSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// ゲーム再生中か
	bool isPlay = context.mode == WorldMode::Play;
	// 状態に応じたdeltaTimeを取得
	float deltaTime = isPlay ? context.deltaTime : context.unscaledDeltaTime;
	world.ForEach<CameraShakeComponent, TransformComponent>([&](
		Entity entity, CameraShakeComponent& shake, TransformComponent& transform) {

			// 前フレームの揺れを除去して基準座標へ戻す
			if (shake.runtimeOffset != Vector3{}) {
				transform.localPos -= shake.runtimeOffset;
				transform.isDirty = true;
				shake.runtimeOffset = Vector3{};
			}
			// エンティティが有効か
			if (!shake.enable || !IsEntityActiveInHierarchy(world, entity)) {
				shake.runtimeTime = 0.0f;
				shake.runtimeActive = false;
				return;
			}
			if (!shake.runtimeActive) {
				shake.runtimeTime = 0.0f;
				shake.runtimeActive = true;
			}
			if (shake.duration <= 0.0f) {
				shake.enable = false;
				shake.runtimeActive = false;
				return;
			}

			// 有効な場合、時間を進める
			shake.runtimeTime += deltaTime;
			// イージング適用
			float easedT = EasedValue(shake.easingType, std::clamp(shake.runtimeTime / shake.duration, 0.0f, 1.0f));

			// 最初の強さから、0.0fに弱める
			Vector3 strength = Vector3::Lerp(shake.strength, Vector3::AnyInit(0.0f), easedT);
			// オフセット
			shake.runtimeOffset = RandomGenerator::Generate(-1.0f, 1.0f) * strength;

			// トランスフォームにオフセットを加算
			transform.localPos += shake.runtimeOffset;
			transform.isDirty = true;

			// 時間経過で終了
			if (shake.duration <= shake.runtimeTime) {

				// フラグで停止
				shake.enable = false;
				shake.runtimeActive = false;
			}
		});
}
