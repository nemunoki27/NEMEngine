#include "DecalProjectorComponent.h"

//============================================================================
//	DecalProjectorComponent functions
//============================================================================
void Engine::from_json(const nlohmann::json& in, DecalProjectorComponent& component) {

	// Materialと投影範囲を復元する
	component.material = ParseAssetID(in, "material");
	ReadMaterialInstance(in.value("materialInstance", nlohmann::json::object()), component.materialInstance);
	component.enabled = in.value("enabled", component.enabled);
	component.size = Vector3::FromJson(in.value("size", component.size.ToJson()));
	component.pivot = Vector3::FromJson(in.value("pivot", component.pivot.ToJson()));
	component.color = Color4::FromJson(in.value("color", component.color.ToJson()));
	component.fadeFactor = in.value("fadeFactor", component.fadeFactor);
	component.uvScale = Vector2::FromJson(in.value("uvScale", component.uvScale.ToJson()));
	component.uvBias = Vector2::FromJson(in.value("uvBias", component.uvBias.ToJson()));
	component.uvRotation = in.value("uvRotation", component.uvRotation);
	component.sortOrder = in.value("sortOrder", component.sortOrder);
	component.drawDistance = in.value("drawDistance", component.drawDistance);
	component.fadeScale = in.value("fadeScale", component.fadeScale);
	component.startAngleFade = in.value("startAngleFade", component.startAngleFade);
	component.endAngleFade = in.value("endAngleFade", component.endAngleFade);
	component.renderingLayerMask = in.value("renderingLayerMask", component.renderingLayerMask) & kRenderingLayerMaskBits;
	component.receiverRenderingLayerMask = in.value("receiverRenderingLayerMask", component.receiverRenderingLayerMask) &
		kRenderingLayerMaskBits;
}

void Engine::to_json(nlohmann::json& out, const DecalProjectorComponent& component) {

	// 実行中の経過時間を含めず編集値だけを保存する
	out = nlohmann::json::object();
	out["material"] = ToAssetReferenceJson(component.material);
	out["materialInstance"] = WriteMaterialInstance(component.materialInstance);
	out["enabled"] = component.enabled;
	out["size"] = component.size.ToJson();
	out["pivot"] = component.pivot.ToJson();
	out["color"] = component.color.ToJson();
	out["fadeFactor"] = component.fadeFactor;
	out["uvScale"] = component.uvScale.ToJson();
	out["uvBias"] = component.uvBias.ToJson();
	out["uvRotation"] = component.uvRotation;
	out["sortOrder"] = component.sortOrder;
	out["drawDistance"] = component.drawDistance;
	out["fadeScale"] = component.fadeScale;
	out["startAngleFade"] = component.startAngleFade;
	out["endAngleFade"] = component.endAngleFade;
	out["renderingLayerMask"] = component.renderingLayerMask & kRenderingLayerMaskBits;
	out["receiverRenderingLayerMask"] = component.receiverRenderingLayerMask & kRenderingLayerMaskBits;
}
