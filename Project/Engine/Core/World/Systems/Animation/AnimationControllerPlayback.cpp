#include "AnimationPlayerSystem.h"
#include "AnimationControllerPlayback.h"

//============================================================================
//	include
//============================================================================
#include "AnimationGroupLookup.h"
#include "AnimationGroupPlayback.h"
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Controllers/AnimationControllerManager.h>

// c++
#include <algorithm>

bool Engine::AnimationControllerPlayback::Synchronize(AnimationPlayerComponent& player, const SystemContext& context) {

	if (!player.controller) {

		if (!player.runtimeControllerAsset) return false;
		player.runtimeControllerAsset = {};
		player.runtimeControllerGeneration = 0;
		player.runtimeController = {};
		player.runtimeControllerGroups.clear();
		player.runtimeCurrentGroup.clear();
		player.runtimeCurrentClips.clear();
		player.runtimeFromGroup.clear();
		player.runtimeFromClips.clear();
		player.runtimeInTransition = false;
		player.runtimeStarted = false;
		return true;
	}
	if (!context.animationControllerManager) return false;
	const auto* definition = context.animationControllerManager->GetOrLoad(*context.assetDatabase, player.controller);
	if (!definition || (player.runtimeControllerAsset == player.controller &&
		player.runtimeControllerGeneration == definition->generation)) return false;
	const bool sameAsset = player.runtimeControllerAsset == player.controller;
	AnimationControllerRuntime previous = std::move(player.runtimeController);
	AnimationControllerEvaluator::Reset(definition->asset, player.runtimeController);
	// 再読込で残っているParameterと状態を引き継ぐ
	if (sameAsset) {

		for (const auto& [name, value] : previous.parameters) {
			AnimationControllerEvaluator::SetParameter(definition->asset, player.runtimeController, name, value);
		}
		if (std::any_of(definition->asset.states.begin(), definition->asset.states.end(), [&](const auto& state) {
			return state.name == previous.state;
		})) player.runtimeController.state = previous.state;
	}
	player.runtimeControllerGroups = definition->asset.states;
	player.runtimeControllerAsset = player.controller;
	player.runtimeControllerGeneration = definition->generation;
	if (!sameAsset || !AnimationGroupLookup::FindGroup(player, player.runtimeCurrentGroup)) {

		player.runtimeCurrentGroup.clear();
		player.runtimeCurrentClips.clear();
		player.runtimeFromGroup.clear();
		player.runtimeFromClips.clear();
		player.runtimeInTransition = false;
		player.runtimeStarted = false;
		player.runtimeControllerStopped = false;
	}
	return true;
}

void Engine::AnimationPlayerSystem::EvaluateController(AnimationPlayerComponent& player, SystemContext& context) {

	if (!player.runtimeControllerAsset || player.runtimeCurrentGroup.empty() || player.runtimeInTransition || player.runtimeControllerStopped) return;
	if (!context.animationControllerManager) return;
	const auto* definition = context.animationControllerManager->GetOrLoad(*context.assetDatabase, player.runtimeControllerAsset);
	if (!definition) return;
	float normalizedTime = 0.0f;
	// 先頭Clipを状態の基準時計として使う
	if (!player.runtimeCurrentClips.empty()) {

		const auto& runtime = player.runtimeCurrentClips.front();
		const auto* group = AnimationGroupLookup::FindGroup(player, player.runtimeCurrentGroup);
		const auto* state = group ? AnimationGroupLookup::FindStateInGroup(*group, runtime.stateName) : nullptr;
		const auto* clip = state ? context.animationClipManager->GetOrLoad(*context.assetDatabase, state->clip) : nullptr;
		if (clip) normalizedTime = static_cast<float>(runtime.normalizedTime);
	}
	player.runtimeController.state = player.runtimeCurrentGroup;
	const auto transition = AnimationControllerEvaluator::Evaluate(definition->asset, player.runtimeController, normalizedTime);
	if (transition) {
		// 既存のグループCrossFadeへ遷移を渡す
		player.runtimePlayRequest = definition->asset.transitions[*transition].to;
		player.runtimePlayFade = definition->asset.transitions[*transition].duration;
	}
}
