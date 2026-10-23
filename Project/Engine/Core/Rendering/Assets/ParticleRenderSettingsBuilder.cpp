#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <utility>

//============================================================================
//	ParticleRenderSettingsBuilder
//============================================================================
Engine::ParticleRenderSettings Engine::MakeParticleRenderSettings(
	PrimitiveRenderSpace space, const ParticleSystemSettings& group) {

	// 共通の形状とMaterialを描画用の値へ移す
	ParticleRenderSettings settings{};
	settings.space = space;
	settings.shape = group.shape;
	settings.plane = group.plane;
	settings.crossPlane = group.crossPlane;
	settings.ring = group.ring;
	settings.cylinder = group.cylinder;
	settings.sphere = group.sphere;
	settings.hemisphere = group.hemisphere;
	settings.cube = group.cube;
	settings.model = group.model;
	settings.material = group.material;
	settings.blendMode = group.blendMode;
	settings.queue = group.queue;
	settings.renderingLayerMask = group.renderingLayerMask & kRenderingLayerMaskBits;
	settings.billboardAxes = group.billboardAxes;
	settings.trail = group.trail;
	settings.emitter = group.emitter;
	// フェーズごとのマテリアルと形状アニメの有無を集める
	settings.phaseMaterials.reserve(group.phases.size());
	settings.phaseMaterialSettings.reserve(group.phases.size());
	for (const ParticlePhaseSettings& phase : group.phases) {

		settings.phaseMaterials.emplace_back(phase.material);
		settings.phaseMaterialSettings.emplace_back(phase.materialSettings);
		for (const ParticleModuleEntry& entry : phase.modules) {
			if (entry.id == "ShapeOverLifetime") {
				settings.shapeOverLifetime = true;
			}
			if (entry.id == "CustomShaderParameter") {

				const auto parameters = entry.params.find("parameters");
				if (parameters == entry.params.end() || !parameters->is_object()) {
					continue;
				}
				for (auto parameter = parameters->begin(); parameter != parameters->end(); ++parameter) {

					ParticleMaterialAnimatedParameter value{};
					from_json(parameter.value(), value);
					settings.phaseMaterialSettings.back().parameters[parameter.key()] = std::move(value);
				}
			}

		}
	}
	if (settings.phaseMaterials.empty()) {
		settings.phaseMaterials.emplace_back();
		settings.phaseMaterialSettings.emplace_back();
	}
	return settings;
}
