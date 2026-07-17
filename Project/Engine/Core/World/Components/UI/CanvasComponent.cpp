#include "CanvasComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	CanvasComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, CanvasComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.scaleMode = EnumAdapter<CanvasScaleMode>::FromString(
		in.value("scaleMode", "ScaleWithScreenSize")).value_or(component.scaleMode);
	component.scaleFactor = in.value("scaleFactor", component.scaleFactor);
	component.matchWidthOrHeight = in.value("matchWidthOrHeight", component.matchWidthOrHeight);
	component.sortingLayer = in.value("sortingLayer", component.sortingLayer);
	component.order = in.value("order", component.order);
	component.blockGameplayInput = in.value("blockGameplayInput", component.blockGameplayInput);
	component.mouseHoverSelect = in.value("mouseHoverSelect", component.mouseHoverSelect);
	component.wrapNavigation = in.value("wrapNavigation", component.wrapNavigation);
	component.repeatDelay = in.value("repeatDelay", component.repeatDelay);
	component.repeatInterval = in.value("repeatInterval", component.repeatInterval);
	component.stickThreshold = in.value("stickThreshold", component.stickThreshold);
	component.firstSelectedLocalFileID = UIComponentSerialization::ReadEntityReference(in, "firstSelected");
}

void Engine::to_json(nlohmann::json& out, const CanvasComponent& component) {

	out["enabled"] = component.enabled;
	out["scaleMode"] = EnumAdapter<CanvasScaleMode>::ToString(component.scaleMode);
	out["scaleFactor"] = component.scaleFactor;
	out["matchWidthOrHeight"] = component.matchWidthOrHeight;
	out["sortingLayer"] = component.sortingLayer;
	out["order"] = component.order;
	out["blockGameplayInput"] = component.blockGameplayInput;
	out["mouseHoverSelect"] = component.mouseHoverSelect;
	out["wrapNavigation"] = component.wrapNavigation;
	out["repeatDelay"] = component.repeatDelay;
	out["repeatInterval"] = component.repeatInterval;
	out["stickThreshold"] = component.stickThreshold;
	out["firstSelected"] = UIComponentSerialization::WriteEntityReference(component.firstSelectedLocalFileID);
}
