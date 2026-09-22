#pragma once

//============================================================================
//	include
//============================================================================
#include "UIInputTypes.h"

namespace Engine::UISelectableAnimation {

	void CaptureAnimationBaseValue(ECSWorld& world, Entity entity,
		const std::string& componentName, const std::string& propertyPath,
		AnimationValueType valueType, std::vector<AnimationPreviewBaseValue>& out);

	void CaptureAnimationBaseValues(ECSWorld& world, Entity entity,
		const UISelectableComponent& selectable, SystemContext& context, std::vector<AnimationPreviewBaseValue>& out);

	void RestoreAnimationBaseValues(ECSWorld& world, Entity entity,
		std::span<const AnimationPreviewBaseValue> baseValues);

	void ApplyAnimationClipOnce(ECSWorld& world, Entity entity, const AnimationClipAsset& clip, float time,
		std::span<const AnimationPreviewBaseValue> baseValues);

	void PlayStateSound(const UITransitionStyle& style, SystemContext& context);

	bool ConfigureAnimationRuntime(ECSWorld& world, Entity entity, const UISelectableComponent& selectable,
		UISelectableRuntimeComponent& selectableRuntime, SystemContext& context,
		UISelectableAnimationRuntime& animationRuntime);

	bool UpdateSelectableAnimation(ECSWorld& world, Entity entity, const UISelectableComponent& selectable,
		UISelectableRuntimeComponent& selectableRuntime, SystemContext& context,
		UISelectableAnimationRuntime& animationRuntime, float deltaTime);

	bool IsSubmitTransitionFinished(const UISelectableComponent& selectable,
		const UISelectableRuntimeComponent& selectableRuntime, const UISelectableAnimationRuntime* animationRuntime);
}
