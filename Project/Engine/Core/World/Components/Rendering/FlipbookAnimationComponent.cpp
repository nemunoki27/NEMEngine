#include "FlipbookAnimationComponent.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Flipbook/FlipbookTileLayout.h>

//============================================================================
//	FlipbookAnimationComponent structMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, FlipbookAnimationComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.loop = in.value("loop", component.loop);
	component.loopInterval = in.value("loopInterval", component.loopInterval);
	component.playInEditMode = in.value("playInEditMode", component.playInEditMode);
	component.endAnimUnDisplay = in.value("endAnimUnDisplay", component.endAnimUnDisplay);
	ReadFlipbookTileLayout(in, component.tilesX, component.tilesY);
	component.duration = in.value("duration", component.duration);
	component.easingType = EnumAdapter<EasingType>::FromString(
		in.value("easingType", "EaseInSine")).value_or(EasingType::EaseInSine);
}

void Engine::to_json(nlohmann::json& out, const FlipbookAnimationComponent& component) {

	out["enabled"] = component.enabled;
	out["loop"] = component.loop;
	out["loopInterval"] = component.loopInterval;
	out["playInEditMode"] = component.playInEditMode;
	out["endAnimUnDisplay"] = component.endAnimUnDisplay;
	WriteFlipbookTileLayout(out, component.tilesX, component.tilesY);
	out["duration"] = component.duration;
	out["easingType"] = EnumAdapter<EasingType>::ToString(component.easingType);
}
