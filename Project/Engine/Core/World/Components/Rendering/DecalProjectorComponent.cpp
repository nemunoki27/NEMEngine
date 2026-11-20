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
	component.color = Color4::FromJson(in.value("color", Color4::White().ToJson()));
	component.fadeFactor = in.value("fadeFactor", 1.0f);
	component.uvScale = Vector2::FromJson(in.value("uvScale", Vector2::AnyInit(1.0f).ToJson()));
	component.uvBias = Vector2::FromJson(in.value("uvBias", Vector2{}.ToJson()));
	component.uvRotation = in.value("uvRotation", 0.0f);
	component.hasLegacyUV = in.contains("uvScale") || in.contains("uvBias") || in.contains("uvRotation");
	component.uvCorrectionU = Vector3::FromJson(in.value("uvCorrectionU", Vector3{}.ToJson()));
	component.uvCorrectionV = Vector3::FromJson(in.value("uvCorrectionV", Vector3{}.ToJson()));
	component.uvCorrectionScaleOffset = Vector2::FromJson(in.value("uvCorrectionScaleOffset", Vector2{}.ToJson()));
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
	if (component.color != Color4::White()) out["color"] = component.color.ToJson();
	if (component.fadeFactor != 1.0f) out["fadeFactor"] = component.fadeFactor;
	if (component.hasLegacyUV) {
		out["uvScale"] = component.uvScale.ToJson();
		out["uvBias"] = component.uvBias.ToJson();
		out["uvRotation"] = component.uvRotation;
	}
	if (component.uvCorrectionU != Vector3{}) out["uvCorrectionU"] = component.uvCorrectionU.ToJson();
	if (component.uvCorrectionV != Vector3{}) out["uvCorrectionV"] = component.uvCorrectionV.ToJson();
	if (component.uvCorrectionScaleOffset != Vector2{}) out["uvCorrectionScaleOffset"] = component.uvCorrectionScaleOffset.ToJson();
	out["sortOrder"] = component.sortOrder;
	out["drawDistance"] = component.drawDistance;
	out["fadeScale"] = component.fadeScale;
	out["startAngleFade"] = component.startAngleFade;
	out["endAngleFade"] = component.endAngleFade;
	out["renderingLayerMask"] = component.renderingLayerMask & kRenderingLayerMaskBits;
	out["receiverRenderingLayerMask"] = component.receiverRenderingLayerMask & kRenderingLayerMaskBits;
}
