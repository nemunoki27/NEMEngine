#include "ParticleHemisphereEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

// c++
#include <cmath>

//============================================================================
//	ParticleHemisphereEmitterShape classMethods
//============================================================================
void Engine::ParticleHemisphereEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.sphere.radius = data.value("sphereRadius", settings.sphere.radius);
}

void Engine::ParticleHemisphereEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["sphereRadius"] = settings.sphere.radius;
}

void Engine::ParticleHemisphereEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	// Y上向きの半球面から外向きに飛ばす
	direction = Vector3::Normalize(RandomGenerator::Generate(Vector3::AnyInit(-1.0f), Vector3::AnyInit(1.0f)));
	direction.y = std::abs(direction.y);
	position = direction * settings.sphere.radius;
}

void Engine::ParticleHemisphereEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	renderer->DrawHemisphere(center, settings.sphere.radius, rotation, Color4::Red());
#endif
}

bool Engine::ParticleHemisphereEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {
#if defined(NEM_EDITOR_UI_ENABLED)

	return MyGUI::DragFloat("半径", settings.sphere.radius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
#else
	(void)settings;
	return false;
#endif
}
