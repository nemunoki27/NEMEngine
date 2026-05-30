#include "InvertedHullOutlineComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	InvertedHullOutlineComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, InvertedHullOutlineComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	// 幅は負値を許容しない
	component.width = (std::max)(0.0f, in.value("width", component.width));
	component.color = Color4::FromJson(in.value("color", nlohmann::json{}));
	component.expansionMode = EnumAdapter<OutlineExpansionMode>::FromString(
		in.value("expansionMode", "NormalDirection"))
		.value_or(OutlineExpansionMode::NormalDirection);
	component.widthMode = EnumAdapter<OutlineWidthMode>::FromString(
		in.value("widthMode", "ModelUnits"))
		.value_or(OutlineWidthMode::ModelUnits);
	component.cameraZOffset = in.value("cameraZOffset", component.cameraZOffset);
	component.useBakedNormal = in.value("useBakedNormal", component.useBakedNormal);
	component.bakedNormalTexture = ParseAssetID(in, "bakedNormalTexture");
	component.useOutlineSampler = in.value("useOutlineSampler", component.useOutlineSampler);
	component.outlineSamplerTexture = ParseAssetID(in, "outlineSamplerTexture");
	component.useStencil = in.value("useStencil", component.useStencil);
}

void Engine::to_json(nlohmann::json& out, const InvertedHullOutlineComponent& component) {

	out["enabled"] = component.enabled;
	out["width"] = component.width;
	out["color"] = component.color.ToJson();
	out["expansionMode"] = EnumAdapter<OutlineExpansionMode>::ToString(component.expansionMode);
	out["widthMode"] = EnumAdapter<OutlineWidthMode>::ToString(component.widthMode);
	out["cameraZOffset"] = component.cameraZOffset;
	out["useBakedNormal"] = component.useBakedNormal;
	out["bakedNormalTexture"] = ToString(component.bakedNormalTexture);
	out["useOutlineSampler"] = component.useOutlineSampler;
	out["outlineSamplerTexture"] = ToString(component.outlineSamplerTexture);
	out["useStencil"] = component.useStencil;
}
