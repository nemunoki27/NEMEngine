#include "CameraModifierSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraModifier.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

//============================================================================
//	CameraModifierSystem classMethods
//============================================================================

void CameraModifierSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// ゲーム再生中か
	bool isPlay = context.mode == WorldMode::Play;
	// 状態に応じたdeltaTimeを取得
	float deltaTime = isPlay ? context.deltaTime : context.unscaledDeltaTime;
	world.ForEach<CameraModifier, TransformComponent>([&](
		Entity entity, CameraModifier& modifier, TransformComponent& transform) {

			// エンティティが有効か
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// 無効なら処理しない
			if (modifier.shake.enable) {


			}
		});
}