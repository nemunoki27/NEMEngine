#include "ParticleSurfaceSettings.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

void Engine::to_json(nlohmann::json& out, const ParticleSurfaceSettings& value) {

	out = {{"softParticles", value.softParticles}, {"softDistance", value.softDistance},
		{"lit", value.lit}, {"receiveShadows", value.receiveShadows}, {"receiveIBL", value.receiveIBL},
		{"normalTexture", ToAssetReferenceJson(value.normalTexture)}, {"normalScale", value.normalScale},
		{"metallic", value.metallic}, {"roughness", value.roughness}};
}

void Engine::from_json(const nlohmann::json& in, ParticleSurfaceSettings& value) {

	if (!in.is_object()) return;
	value.softParticles = in.value("softParticles", value.softParticles);
	value.softDistance = (std::max)(in.value("softDistance", value.softDistance), 0.0001f);
	value.lit = in.value("lit", value.lit);
	value.receiveShadows = in.value("receiveShadows", value.receiveShadows);
	value.receiveIBL = in.value("receiveIBL", value.receiveIBL);
	value.normalTexture = ParseAssetID(in, "normalTexture");
	value.normalScale = (std::max)(in.value("normalScale", value.normalScale), 0.0f);
	value.metallic = std::clamp(in.value("metallic", value.metallic), 0.0f, 1.0f);
	value.roughness = std::clamp(in.value("roughness", value.roughness), 0.04f, 1.0f);
}
