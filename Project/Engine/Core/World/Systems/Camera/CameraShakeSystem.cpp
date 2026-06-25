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

			// 無効な場合は処理しない
			if (!shake.enable) {
				return;
			}
			// エンティティが有効か
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// 有効な場合、時間を進める
			shake.runtimeTime += deltaTime;
			// イージング適用
			float easedT = EasedValue(shake.easingType, std::clamp(shake.runtimeTime / shake.duration, 0.0f, 1.0f));

			// 最初の強さから、0.0fに弱める
			Vector3 strength = Vector3::Lerp(shake.strength, Vector3::AnyInit(0.0f), easedT);
			// オフセット
			Vector3 offset = RandomGenerator::Generate(-1.0f, 1.0f) * strength;

			// トランスフォームにオフセットを加算
			transform.localPos += offset;

			// 時間経過で終了
			if (shake.duration < shake.runtimeTime) {

				// フラグで停止
				shake.enable = false;
			}
		});
}