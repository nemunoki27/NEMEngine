#include "FlipbookAnimationComponent.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	FlipbookAnimationComponent structMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, FlipbookAnimationComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.loop = in.value("loop", component.loop);
	component.playInEditMode = in.value("playInEditMode", component.playInEditMode);
	component.endAnimUnDisplay = in.value("endAnimUnDisplay", component.endAnimUnDisplay);
	component.tilesX = in.value("tilesX", component.tilesX);
	component.tilesY = in.value("tilesY", component.tilesY);
	component.duration = in.value("duration", component.duration);
	component.easingType = EnumAdapter<EasingType>::FromString(in.value("easingType", "EaseInSine")).value();
}

void Engine::to_json(nlohmann::json& out, const FlipbookAnimationComponent& component) {

	out["enabled"] = component.enabled;
	out["loop"] = component.loop;
	out["playInEditMode"] = component.playInEditMode;
	out["endAnimUnDisplay"] = component.endAnimUnDisplay;
	out["tilesX"] = component.tilesX;
	out["tilesY"] = component.tilesY;
	out["duration"] = component.duration;
	out["easingType"] = EnumAdapter<EasingType>::ToString(component.easingType);
}