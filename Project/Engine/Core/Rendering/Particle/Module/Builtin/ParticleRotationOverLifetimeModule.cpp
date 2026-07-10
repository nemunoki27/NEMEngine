#include "ParticleRotationOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Random/RandomGenerator.h>
#include <Engine/Core/Foundation/Math/Math.h>

//============================================================================
//	ParticleRotationOverLifetimeModule internal
//============================================================================
namespace {

	// 数値ならZ軸のみの旧スキーマとして読む
	void ReadAxisValue(const nlohmann::json& params, const char* key, Engine::Vector3& out) {

		const auto it = params.find(key);
		if (it == params.end()) {
			return;
		}
		if (it->is_number()) {
			out.z = it->get<float>();
		} else {
			out = Engine::Vector3::FromJson(*it);
		}
	}
}

//============================================================================
//	ParticleRotationOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleRotationOverLifetimeModule::FromJson(const nlohmann::json& params) {

	ReadAxisValue(params, "initialMin", initialMin_);
	ReadAxisValue(params, "initialMax", initialMax_);
	ReadAxisValue(params, "speedMin", speedMin_);
	ReadAxisValue(params, "speedMax", speedMax_);
}

nlohmann::json Engine::ParticleRotationOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["initialMin"] = initialMin_.ToJson();
	params["initialMax"] = initialMax_.ToJson();
	params["speedMin"] = speedMin_.ToJson();
	params["speedMax"] = speedMax_.ToJson();
	return params;
}

void Engine::ParticleRotationOverLifetimeModule::OnSpawn(std::span<Particle> newborn) {

	for (Particle& particle : newborn) {

		particle.rotation = Quaternion::FromEulerDegrees(RandomGenerator::Generate(initialMin_, initialMax_));
		particle.rotationSpeed = RandomGenerator::Generate(speedMin_, speedMax_);
	}
}

void Engine::ParticleRotationOverLifetimeModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {

		// 角速度ベクトルの軸回りにクォータニオンで積分する
		const float speed = Vector3::Length(particle.rotationSpeed);
		if (speed <= 0.0f) {
			continue;
		}
		const Vector3 axis = particle.rotationSpeed * (1.0f / speed);
		particle.rotation = Quaternion::Normalize(Quaternion::MakeAxisAngle(
			axis, speed * Math::radian * deltaTime) * particle.rotation);
	}
}

bool Engine::ParticleRotationOverLifetimeModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragVector3("初期回転最小", initialMin_, ParticleGui::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragVector3("初期回転最大", initialMax_, ParticleGui::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragVector3("角速度最小", speedMin_, ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragVector3("角速度最大", speedMax_, ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	return changed;
}