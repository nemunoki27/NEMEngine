#include "AnimationGroupLookup.h"
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationGroupLookup {

	const Engine::AnimationState* FindStateInGroup(const Engine::AnimationGroup& group, const std::string& name) {

		for (const Engine::AnimationState& state : group.states) {
			if (state.name == name) {
				return &state;
			}
		}
		return nullptr;
	}

	bool SameBinding(const Engine::AnimationPropertyBinding& lhs, const Engine::AnimationPropertyBinding& rhs) {

		return AnimationValueOperations::SameBinding(lhs, rhs);
	}

	const Engine::AnimationGroup* FindGroup( const AnimationPlayerComponent& player, const std::string& name) {

		if (name.empty()) {
			return nullptr;
		}
		if (name == player.runtimeDirectGroup.name) return &player.runtimeDirectGroup;
		// Controllerの状態は共有Assetの実行snapshotから参照する
		for (const AnimationGroup& group : player.runtimeControllerGroups) {
			if (group.name == name) return &group;
		}
		for (const AnimationGroup& group : player.groups) {
			if (group.name == name) {
				return &group;
			}
		}
		return nullptr;
	}
}
