#include "SpriteRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	SpriteRendererComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, SpriteRendererComponent& component) {

	component.texture = ParseAssetID(in, "texture");
	component.material = ParseAssetID(in, "material");
	ReadMaterialParameterOverrides(in.value("parameterOverrides", nlohmann::json::object()), component.parameterOverrides);
	component.size = Vector2::FromJson(in.value("size", nlohmann::json()));
	component.pivot = Vector2::FromJson(in.value("pivot", nlohmann::json()));
	component.color = Color4::FromJson(in.value("color", nlohmann::json()));
	ReadRenderCommonFields(in, component.layer, component.order, component.visible, component.blendMode, component.queue);
}

void Engine::to_json(nlohmann::json& out, const SpriteRendererComponent& component) {

	out["texture"] = ToAssetReferenceJson(component.texture);
	out["material"] = ToAssetReferenceJson(component.material);
	out["parameterOverrides"] = WriteMaterialParameterOverrides(component.parameterOverrides);
	out["size"] = component.size.ToJson();
	out["pivot"] = component.pivot.ToJson();
	out["color"] = component.color.ToJson();
	WriteRenderCommonFields(out, component.layer, component.order, component.visible, component.blendMode, component.queue);
}
