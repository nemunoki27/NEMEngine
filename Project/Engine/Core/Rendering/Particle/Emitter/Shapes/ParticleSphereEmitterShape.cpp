#include "ParticleSphereEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

//============================================================================
//	ParticleSphereEmitterShape classMethods
//============================================================================
void Engine::ParticleSphereEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.sphere.radius = data.value("sphereRadius", settings.sphere.radius);
}

void Engine::ParticleSphereEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["sphereRadius"] = settings.sphere.radius;
}

void Engine::ParticleSphereEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// 球面上から外向きに飛ばす
	direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
	position = direction * settings.sphere.radius;
}

void Engine::ParticleSphereEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, [[maybe_unused]] const Quaternion& rotation, [[maybe_unused]] bool is2D) const {

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawSphere(center, settings.sphere.radius, Color4::Red(), 1.0f);
}

bool Engine::ParticleSphereEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {

	bool result = MyGUI::DragFloat("半径", settings.sphere.radius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;

	return result;
}