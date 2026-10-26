#include "ParticleSimulationValidation.h"

bool Engine::IsParticleModuleGPUOnly(std::string_view moduleID) {

	return moduleID == "ForceField" || moduleID == "VectorField" || moduleID == "SplineMovement" || moduleID == "Collision";
}

std::string Engine::ValidateParticleSimulationSettings(const ParticleSystemSettings& settings, ParticleSimulationTarget target) {

	if (target == ParticleSimulationTarget::GPU) return {};
	if (settings.surface.softParticles || settings.surface.lit) return "粒子の深度フェードと標準照明にはGPU実行が必要です";
	if (settings.source.kind != ParticleSourceKind::Analytic) return "メッシュ、画像、Cameraからの粒子発生にはGPU実行が必要です";
	if (!settings.feedback.events.empty() || settings.feedback.paint.enabled || settings.feedback.decal.enabled)
		return "粒子イベント、塗装、衝突DecalにはGPU実行が必要です";
	for (const auto& phase : settings.phases) {
		for (const auto& module : phase.modules) {
			if (module.enabled && IsParticleModuleGPUOnly(module.id))
				return "Particle Module " + module.id + "にはGPU実行が必要です";
		}
	}
	return {};
}
