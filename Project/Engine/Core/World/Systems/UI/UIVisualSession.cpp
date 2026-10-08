#include "UIVisualSession.h"
#include "UISelectableStyle.h"
#include "UISelectableAnimation.h"

using namespace Engine::UISelectableStyle;
using namespace Engine::UISelectableAnimation;

void Engine::UIVisualSession::DiscardInactive(ECSWorld& world, const std::unordered_set<UUID>& activeSelectableEntities) {

	// Canvasから外れたUIはクリップ適用前の値へ戻して再生状態を破棄する
	for (auto it = animationRuntimes_.begin(); it != animationRuntimes_.end();) {

		if (activeSelectableEntities.contains(it->first)) {
			++it;
			continue;
		}
		const Entity entity = world.FindByUUID(it->first);
		if (world.IsAlive(entity)) {
			if (it->second.applied) {
				RestoreAnimationBaseValues(world, entity, it->second.baseValues);
			}
		}
		it = animationRuntimes_.erase(it);
	}

}

void Engine::UIVisualSession::ResetSubmitState(UUID entityUUID) {

	if (auto runtime = animationRuntimes_.find(entityUUID);
		runtime != animationRuntimes_.end()) {
		if (runtime->second.stateInitialized &&
			runtime->second.state == GetStyleIndex(UISelectableState::Submitted)) {
			runtime->second.state = static_cast<uint8_t>(
				GetStyleIndex(UISelectableState::Selected));
		}
	}
}

Engine::UISelectableAnimationRuntime* Engine::UIVisualSession::UpdateAnimation(ECSWorld& world, UISelectableEntry& entry, SystemContext& context) {

	UISelectableRuntimeComponent& selectableRuntime = *entry.runtime;
	UISelectableAnimationRuntime* animationRuntime = nullptr;
	auto runtime = animationRuntimes_.find(world.GetUUID(entry.entity));
	if (runtime != animationRuntimes_.end() || HasStateTransitionRuntime(*entry.selectable)) {
		if (runtime == animationRuntimes_.end()) {
			runtime = animationRuntimes_.try_emplace(world.GetUUID(entry.entity)).first;
		}
		animationRuntime = &runtime->second;
		UpdateSelectableAnimation(world, entry.entity,
			*entry.selectable, selectableRuntime,
			context, runtime->second, context.unscaledDeltaTime);
	}
	return animationRuntime;
}

void Engine::UIVisualSession::RestoreAll(ECSWorld& world) {

	for (const auto& [uuid, runtime] : animationRuntimes_) {

		const Entity entity = world.FindByUUID(uuid);
		if (world.IsAlive(entity) && runtime.applied) {
			RestoreAnimationBaseValues(world, entity, runtime.baseValues);
		}
	}
	animationRuntimes_.clear();
}
