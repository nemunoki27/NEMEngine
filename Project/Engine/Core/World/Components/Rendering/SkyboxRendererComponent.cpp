#include "SkyboxRendererComponent.h"

//============================================================================
//	SkyboxRendererComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, SkyboxRendererComponent& component) {

	component.cubemapTexture = ParseAssetID(in, "cubemapTexture");
	// color未保存の既存シーンは白を維持する、空jsonは黒になり真っ黒化するため
	if (in.contains("color")) {
		component.color = Color4::FromJson(in["color"]);
	}
	component.visible = in.value("visible", component.visible);
}

void Engine::to_json(nlohmann::json& out, const SkyboxRendererComponent& component) {

	out["cubemapTexture"] = ToAssetReferenceJson(component.cubemapTexture);
	out["color"] = component.color.ToJson();
	out["visible"] = component.visible;
}
