#include "AnimationBaseValues.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Evaluation/AnimationPropertySnapshot.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationBaseValues {

	void CaptureBaseValues(ECSWorld& world, const Entity& entity,
		const AnimationPlayerComponent& player, SystemContext& context, std::vector<AnimationPreviewBaseValue>& out) {

		out.clear();
		if (!context.assetDatabase || !context.animationClipManager) {
			return;
		}

		// 全グループの全クリップが触るプロパティの和集合を一度だけ捕捉する
		bool anyRelative = false;
		const auto captureGroups = [&](const std::vector<AnimationGroup>& groups) {
			for (const AnimationGroup& group : groups) {
				for (const AnimationState& state : group.states) {

					const AnimationClipAsset* clip = context.animationClipManager->GetOrLoad(*context.assetDatabase, state.clip);
					if (!clip) {
						continue;
					}
					anyRelative |= state.relativeTransform;
					AnimationPropertySnapshot::CaptureClip(world, entity, *clip, false, out);
				}
			}
		};
		captureGroups(player.groups);
		captureGroups(player.runtimeControllerGroups);
		if (!player.runtimeDirectGroup.name.empty()) captureGroups({ player.runtimeDirectGroup });

		// 向き相対クリップは基準の位置/回転が必須なので、未アニメでも捕捉しておく
		if (anyRelative) {
			AnimationPropertySnapshot::CaptureRelativeTransform(world, entity, out);
		}
	}

	void RestoreBaseValues(ECSWorld& world, const Entity& entity, const std::vector<AnimationPreviewBaseValue>& base) {

		AnimationPropertySnapshot::Restore(world, entity, base);
	}
}
