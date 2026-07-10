#include "ParticleTorusEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cmath>
#include <numbers>

//============================================================================
//	ParticleTorusEmitterShape classMethods
//============================================================================
void Engine::ParticleTorusEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	settings.torus.radius = data.value("torusRadius", settings.torus.radius);
	settings.torus.thickness = data.value("torusThickness", settings.torus.thickness);
}

void Engine::ParticleTorusEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	data["torusRadius"] = settings.torus.radius;
	data["torusThickness"] = settings.torus.thickness;
}

void Engine::ParticleTorusEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, [[maybe_unused]] bool is2D) const {

	constexpr float pi = std::numbers::pi_v<float>;

	// 主円周上の管内から管の外向きに飛ばす
	const float mainAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
	const float tubeAngle = RandomGenerator::Generate(0.0f, pi * 2.0f);
	const float tubeRadius = RandomGenerator::Generate(0.0f, settings.torus.thickness);
	const Vector3 radial(std::cos(mainAngle), 0.0f, std::sin(mainAngle));
	const Vector3 tubeDir = radial * std::cos(tubeAngle) + Vector3(0.0f, std::sin(tubeAngle), 0.0f);
	position = radial * settings.torus.radius + tubeDir * tubeRadius;
	direction = tubeDir;
}

void Engine::ParticleTorusEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, [[maybe_unused]] bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Color4 color = Color4::Red();

	// 主円周を管の内外2本の円で表す
	constexpr uint32_t kDivision = 24;
	constexpr float kStep = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kDivision);
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = kStep * static_cast<float>(i);
		const float angle1 = kStep * static_cast<float>(i + 1);
		for (const float radius : { settings.torus.radius - settings.torus.thickness,
			settings.torus.radius + settings.torus.thickness }) {

			const Vector3 p0 = center + Vector3::Transform(
				Vector3(std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius), rotationMatrix);
			const Vector3 p1 = center + Vector3::Transform(
				Vector3(std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius), rotationMatrix);
			renderer->DrawLine(p0, p1, color);
		}
	}
#endif
}

bool Engine::ParticleTorusEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {

	bool changed = false;
	changed |= MyGUI::DragFloat("主半径", settings.torus.radius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("管半径", settings.torus.thickness, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	return changed;
}
