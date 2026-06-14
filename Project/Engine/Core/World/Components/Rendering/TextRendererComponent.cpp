#include "TextRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	TextRendererComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, TextRendererComponent& component) {

	component.font = ParseAssetID(in, "font");
	component.material = ParseAssetID(in, "material");
	component.text = in.value("text", component.text);
	component.fontSize = in.value("fontSize", component.fontSize);
	component.charSpacing = in.value("charSpacing", component.charSpacing);
	// 未設定なら既定の左上原点を保つためcontainsで判定する
	if (in.contains("pivot")) {
		component.pivot = Vector2::FromJson(in["pivot"]);
	}
	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	component.enableOutline = in.value("enableOutline", component.enableOutline);
	// 未設定なら既定の黒を保つためcontainsで判定する
	if (in.contains("outlineColor")) {
		component.outlineColor = Color4::FromJson(in["outlineColor"]);
	}
	component.outlineWidth = in.value("outlineWidth", component.outlineWidth);
	// 文字ごとのトランスフォーム
	component.charTransforms.clear();
	if (in.contains("charTransforms") && in["charTransforms"].is_array()) {
		for (const auto& charData : in["charTransforms"]) {

			TextCharTransform charTransform{};
			charTransform.translation = Vector2(charData.value("tx", 0.0f), charData.value("ty", 0.0f));
			charTransform.rotation = charData.value("rotation", 0.0f);
			charTransform.scale = Vector2(charData.value("sx", 1.0f), charData.value("sy", 1.0f));
			component.charTransforms.emplace_back(charTransform);
		}
	}
	component.queue = RenderPhaseFromString(in.value("queue", std::string(ToString(component.queue))), component.queue);
	component.layer = in.value("layer", component.layer);
	component.order = in.value("order", component.order);
	component.visible = in.value("visible", component.visible);
	component.blendMode = EnumAdapter<BlendMode>::FromString(in.value("blendMode", "Normal")).value();
	// 次元は単純なint表現で保存する、Type2D=0 / Type3D=1
	component.dimension = static_cast<Dimension>(in.value("dimension", static_cast<int>(component.dimension)));
	component.worldScale = in.value("worldScale", component.worldScale);

	// ランタイムキャッシュはシリアライズしないので初期化しておく
	component.runtimeLayout = {};
}

void Engine::to_json(nlohmann::json& out, const TextRendererComponent& component) {

	out["font"] = ToAssetReferenceJson(component.font);
	out["material"] = ToAssetReferenceJson(component.material);
	out["text"] = component.text;
	out["fontSize"] = component.fontSize;
	out["charSpacing"] = component.charSpacing;
	out["pivot"] = component.pivot.ToJson();
	out["color"] = component.color.ToJson();
	out["enableOutline"] = component.enableOutline;
	out["outlineColor"] = component.outlineColor.ToJson();
	out["outlineWidth"] = component.outlineWidth;
	// 文字ごとのトランスフォーム
	nlohmann::json charArray = nlohmann::json::array();
	for (const TextCharTransform& charTransform : component.charTransforms) {

		nlohmann::json charData;
		charData["tx"] = charTransform.translation.x;
		charData["ty"] = charTransform.translation.y;
		charData["rotation"] = charTransform.rotation;
		charData["sx"] = charTransform.scale.x;
		charData["sy"] = charTransform.scale.y;
		charArray.push_back(charData);
	}
	out["charTransforms"] = charArray;
	out["queue"] = std::string(ToString(component.queue));
	out["layer"] = component.layer;
	out["order"] = component.order;
	out["visible"] = component.visible;
	out["blendMode"] = EnumAdapter<BlendMode>::ToString(component.blendMode);
	out["dimension"] = static_cast<int>(component.dimension);
	out["worldScale"] = component.worldScale;
}
