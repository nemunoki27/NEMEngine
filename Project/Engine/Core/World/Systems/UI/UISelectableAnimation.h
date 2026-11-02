#pragma once

#include "UIInputTypes.h"

namespace Engine::UISelectableAnimation {

	void PlayStateSound(ECSWorld& world, Entity entity, const UITransitionStyle& style, SystemContext& context);
	bool IsSubmitTransitionFinished(const UISelectableComponent& selectable, const UISelectableRuntimeComponent& runtime);
}
