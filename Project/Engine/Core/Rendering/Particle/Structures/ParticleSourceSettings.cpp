#include "ParticleSourceSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

void Engine::from_json(const nlohmann::json& in, ParticleSourceSettings& settings) {

	settings.kind = EnumAdapter<ParticleSourceKind>::FromString(in.value("kind", "Analytic")).value_or(ParticleSourceKind::Analytic);
	settings.mesh = ParseAssetID(in, "mesh");
	settings.texture = ParseAssetID(in, "texture");
	settings.maskTexture = ParseAssetID(in, "maskTexture");
	if (const auto it = in.find("renderer"); it != in.end()) from_json(*it, settings.renderer);
	if (const auto it = in.find("camera"); it != in.end()) from_json(*it, settings.camera);
	if (const auto it = in.find("subMeshes"); it != in.end()) settings.subMeshes = it->get<std::vector<uint32_t>>();
	if (const auto it = in.find("textureSize"); it != in.end()) settings.textureSize = Vector2::FromJson(*it);
	settings.surfaceOffset = in.value("surfaceOffset", settings.surfaceOffset);
	settings.sequential = in.value("sequential", settings.sequential);
	settings.attachToSurface = in.value("attachToSurface", settings.attachToSurface);
	settings.inheritColor = in.value("inheritColor", settings.inheritColor);
	settings.inheritVelocity = in.value("inheritVelocity", settings.inheritVelocity);
	settings.useMask = in.value("useMask", settings.useMask);
	settings.maskFromVertexColors = in.value("maskFromVertexColors", settings.maskFromVertexColors);
	settings.maskChannel = EnumAdapter<ParticleSourceChannel>::FromString(in.value("maskChannel", "Alpha"))
		.value_or(ParticleSourceChannel::Alpha);
	settings.maskMinimum = in.value("maskMinimum", settings.maskMinimum);
	settings.maskMaximum = in.value("maskMaximum", settings.maskMaximum);
}

void Engine::to_json(nlohmann::json& out, const ParticleSourceSettings& settings) {

	out = {{"kind", EnumAdapter<ParticleSourceKind>::ToString(settings.kind)},
		{"mesh", settings.mesh ? ToString(settings.mesh) : std::string{}}, {"renderer", settings.renderer},
		{"texture", settings.texture ? ToString(settings.texture) : std::string{}}, {"camera", settings.camera},
		{"subMeshes", settings.subMeshes}, {"textureSize", settings.textureSize.ToJson()}, {"surfaceOffset", settings.surfaceOffset},
		{"sequential", settings.sequential}, {"attachToSurface", settings.attachToSurface}, {"inheritColor", settings.inheritColor},
		{"inheritVelocity", settings.inheritVelocity}, {"useMask", settings.useMask},
		{"maskFromVertexColors", settings.maskFromVertexColors},
		{"maskTexture", settings.maskTexture ? ToString(settings.maskTexture) : std::string{}},
		{"maskChannel", EnumAdapter<ParticleSourceChannel>::ToString(settings.maskChannel)},
		{"maskMinimum", settings.maskMinimum}, {"maskMaximum", settings.maskMaximum}};
}
