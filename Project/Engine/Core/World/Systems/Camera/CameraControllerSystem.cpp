#include "CameraControllerSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include "CameraControllerUpdate.h"
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine::CameraControllerUpdate;

//============================================================================
//	CameraControllerSystem classMethods
//============================================================================

void CameraControllerSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// ゲーム再生中か
	bool isPlay = context.mode == WorldMode::Play;
	// 状態に応じたdeltaTimeを取得
	float deltaTime = isPlay ? context.deltaTime : context.unscaledDeltaTime;
	world.ForEach<CameraControllerComponent, TransformComponent>([&](
		Entity entity, CameraControllerComponent& controller, TransformComponent& transform) {

			// 無効再生中なら処理しない
			if (!controller.enabled || (!isPlay && !controller.editorPreview)) {
				return;
			}
			// エンティティが有効か
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// オービット入力はPlay中だけ受け付ける、Editプレビューでマウスがエディタ操作と干渉しないようにする
			const bool allowInput = isPlay;

			// モードごとに使う設定を選ぶ
			bool changed = false;
			if (controller.mode == CameraControlMode::Follow) {

				transform.localPos = ApplyFollow(world, entity, transform, controller.follow, deltaTime, allowInput);
				changed |= controller.follow.enabled;
			} else if (controller.mode == CameraControlMode::LookAt) {

				changed |= ApplyLookAt(world, entity, transform, controller.lookAt, deltaTime);
			} else if (controller.mode == CameraControlMode::FollowLookAt) {

				// 注視は更新後の位置を使うので追従を先に適用する
				transform.localPos = ApplyFollow(world, entity, transform, controller.followLookAt.follow, deltaTime, allowInput);
				changed |= controller.followLookAt.follow.enabled;
				changed |= ApplyLookAt(world, entity, transform, controller.followLookAt.lookAt, deltaTime);
			}
			// パラメータに変更があればトランスフォームを更新させる
			if (changed) {

				MarkTransformSubtreeDirty(world, entity);
			}
		});
}
