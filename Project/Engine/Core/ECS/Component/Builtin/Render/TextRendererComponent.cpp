#include "TextRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	TextRendererComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, TextRendererComponent& component) {

	component.font = ParseAssetID(in, "font");
	component.material = ParseAssetID(in, "material");
	component.text = in.value("text", component.text);
	component.fontSize = in.value("fontSize", component.fontSize);
	component.charSpacing = in.value("charSpacing", component.charSpacing);
	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.queue = in.value("queue", component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();

	// ランタイムキャッシュはシリアライズしないので初期化しておく
	component.runtimeLayout = {};
}

void Engine::to_json(nlohmann::json& out, const TextRendererComponent& component) {

	out["font"] = ToString(component.font);
	out["material"] = ToString(component.material);
	out["text"] = component.text;
	out["fontSize"] = component.fontSize;
	out["charSpacing"] = component.charSpacing;
	out["color"] = component.color.ToJson();
	out["queue"] = component.queue;
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);
}