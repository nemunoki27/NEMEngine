#include "ParticleEmitterComponent.h"

//============================================================================
//	ParticleEmitterComponent structMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, ParticleEmitterComponent& component) {

	component.effect = ParseAssetID(in, "effect");
	component.playing = in.value("playing", component.playing);
	component.playInEditMode = in.value("playInEditMode", component.playInEditMode);
	component.drawEmitterShape = in.value("drawEmitterShape", component.drawEmitterShape);

	ReadRenderCommonFields(in, component.layer, component.order, component.visible, component.blendMode, component.queue);
}

void Engine::to_json(nlohmann::json& out, const ParticleEmitterComponent& component) {

	out["effect"] = ToAssetReferenceJson(component.effect);
	out["playing"] = component.playing;
	out["playInEditMode"] = component.playInEditMode;
	out["drawEmitterShape"] = component.drawEmitterShape;

	WriteRenderCommonFields(out, component.layer, component.order, component.visible, component.blendMode, component.queue);
}
