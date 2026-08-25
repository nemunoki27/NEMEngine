#include "SpriteRendererComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	SpriteRendererComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, SpriteRendererComponent& component) {

	component.material = ParseAssetID(in, "material");
	ReadMaterialInstance(in.value("materialInstance", nlohmann::json::object()), component.materialInstance);
	component.size = Vector2::FromJson(in.value("size", nlohmann::json()));
	component.pivot = Vector2::FromJson(in.value("pivot", nlohmann::json()));
	ReadRenderCommonFields(in, component.layer, component.order, component.visible, component.blendMode, component.queue);
	component.renderingLayerMask = in.value(
		"renderingLayerMask", component.renderingLayerMask) &
		kRenderingLayerMaskBits;
}

void Engine::to_json(nlohmann::json& out, const SpriteRendererComponent& component) {

	out["material"] = ToAssetReferenceJson(component.material);
	out["materialInstance"] = WriteMaterialInstance(component.materialInstance);
	out["size"] = component.size.ToJson();
	out["pivot"] = component.pivot.ToJson();
	WriteRenderCommonFields(out, component.layer, component.order, component.visible, component.blendMode, component.queue);
	out["renderingLayerMask"] = component.renderingLayerMask &
		kRenderingLayerMaskBits;
}
