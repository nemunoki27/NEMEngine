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
	world.ForEach<CameraShakeComponent, CameraShakeRuntimeComponent, TransformComponent>(
		[&](Entity entity, CameraShakeComponent& shake,
			CameraShakeRuntimeComponent& runtime, TransformComponent& transform) {

			// 前フレームの揺れを除去して基準座標へ戻す
			if (runtime.offset != Vector3{}) {
				transform.localPos -= runtime.offset;
				transform.isDirty = true;
				runtime.offset = Vector3{};
			}
			// エンティティが有効か
			if (!shake.enable || !IsEntityActiveInHierarchy(world, entity)) {
				runtime.time = 0.0f;
				runtime.active = false;
				return;
			}
			if (!runtime.active) {
				runtime.time = 0.0f;
				runtime.active = true;
			}
			if (shake.duration <= 0.0f) {
				shake.enable = false;
				runtime.active = false;
				return;
			}

			// 有効な場合、時間を進める
			runtime.time += deltaTime;
			// イージング適用
			float easedT = EasedValue(shake.easingType,
				std::clamp(runtime.time / shake.duration, 0.0f, 1.0f));

			// 最初の強さから、0.0fに弱める
			Vector3 strength = Vector3::Lerp(shake.strength, Vector3::AnyInit(0.0f), easedT);
			// オフセット
			runtime.offset =
				RandomGenerator::Generate(-1.0f, 1.0f) * strength;

			// トランスフォームにオフセットを加算
			transform.localPos += runtime.offset;
			transform.isDirty = true;

			// 時間経過で終了
			if (shake.duration <= runtime.time) {

				// フラグで停止
				shake.enable = false;
				runtime.active = false;
			}
		});
}
