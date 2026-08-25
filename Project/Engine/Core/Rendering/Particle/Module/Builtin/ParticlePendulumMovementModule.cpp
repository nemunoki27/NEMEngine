#include "ParticlePendulumMovementModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>
#include <span>

//============================================================================
//	ParticlePendulumMovementModule classMethods
//============================================================================
void Engine::ParticlePendulumMovementModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("length"); it != params.end()) {
		ReadAnimationSettings(*it, length_);
	}
	if (const auto it = params.find("maxAngle"); it != params.end()) {
		ReadAnimationSettings(*it, maxAngle_);
	}
	if (const auto it = params.find("cycles"); it != params.end()) {
		ReadAnimationSettings(*it, cycles_);
	}
	planeAngle_ = params.value("planeAngle", planeAngle_);
	startPhase_ = params.value("startPhase", startPhase_);
	particlePhaseOffset_ = params.value("particlePhaseOffset", particlePhaseOffset_);
	arcStrength_ = (std::clamp)(params.value("arcStrength", arcStrength_), -1.0f, 1.0f);
	reverse_ = params.value("reverse", reverse_);
}

nlohmann::json Engine::ParticlePendulumMovementModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["length"] = WriteAnimationSettings(length_);
	params["maxAngle"] = WriteAnimationSettings(maxAngle_);
	params["cycles"] = WriteAnimationSettings(cycles_);
	params["planeAngle"] = planeAngle_;
	params["startPhase"] = startPhase_;
	params["particlePhaseOffset"] = particlePhaseOffset_;
	params["arcStrength"] = arcStrength_;
	params["reverse"] = reverse_;
	return params;
}

void Engine::ParticlePendulumMovementModule::OnSpawn(Particle& particle) {

	const PendulumBasis basis = CalculateBasis(particle);
	particle.pos += CalculateOffset(basis, CalculateStartPhase(particle.id), 0.0f);
}

void Engine::ParticlePendulumMovementModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float currentT = particle.age / particle.lifetime;
	const PendulumBasis basis = CalculateBasis(particle);
	const float particleStartPhase = CalculateStartPhase(particle.id);
	if (particle.phaseIndex != particle.previousPhaseIndex) {

		particle.pos += CalculateOffset(basis, particleStartPhase, currentT);
		return;
	}

	const float previousT = particle.previousAge / particle.lifetime;
	particle.pos += CalculateOffset(basis, particleStartPhase, currentT) -
		CalculateOffset(basis, particleStartPhase, previousT);
}

bool Engine::ParticlePendulumMovementModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= DrawAnimationSettings(
		"振り子の長さ", "Length", "開始長さ", "終了長さ", length_, 0.0f, 10000.0f);
	changed |= DrawAnimationSettings(
		"最大振れ角", "MaxAngle", "開始角度", "終了角度", maxAngle_, 0.0f, 180.0f);
	changed |= DrawAnimationSettings(
		"振動回数", "Cycles", "開始回数", "終了回数", cycles_, 0.0f, 1000.0f);

	if (MyGUI::CollapsingHeader("軌道", false)) {

		changed |= MyGUI::DragFloat(
			"振り子平面角度", planeAngle_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"開始位相", startPhase_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"粒子ごとの位相差", particlePhaseOffset_,
			ParticleGui::MakeDragSetting(-360.0f, 360.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"円弧の強さ", arcStrength_,
			ParticleGui::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= MyGUI::Checkbox("逆方向", reverse_);
	}
	return changed;
#else
	return false;
#endif
}

void Engine::ParticlePendulumMovementModule::ReadAnimationSettings(
	const nlohmann::json& in, FloatAnimationSettings& settings) {

	if (!in.is_object()) {
		return;
	}

	settings.start = in.value("start", settings.start);
	settings.end = in.value("end", settings.end);
	settings.easingType = EnumAdapter<EasingType>::FromString(
		in.value("easingType", EnumAdapter<EasingType>::ToString(settings.easingType)))
		.value_or(settings.easingType);
	if (const auto it = in.find("loop"); it != in.end()) {
		from_json(*it, settings.loop);
	}
	settings.useCurve = in.value("useCurve", settings.useCurve);
	if (const auto it = in.find("curve"); it != in.end() && it->is_object()) {
		from_json(*it, settings.curve.channel);
	}
}

nlohmann::json Engine::ParticlePendulumMovementModule::WriteAnimationSettings(
	const FloatAnimationSettings& settings) const {

	nlohmann::json out = nlohmann::json::object();
	out["start"] = settings.start;
	out["end"] = settings.end;
	out["easingType"] = EnumAdapter<EasingType>::ToString(settings.easingType);
	to_json(out["loop"], settings.loop);
	out["useCurve"] = settings.useCurve;
	to_json(out["curve"], settings.curve.channel);
	return out;
}

float Engine::ParticlePendulumMovementModule::EvaluateAnimation(
	const FloatAnimationSettings& settings, float rawT) const {

	const float progress = settings.loop.LoopedT(rawT);
	return settings.useCurve ? settings.curve.Evaluate(progress) :
		Math::Lerp(settings.start, settings.end, EasedValue(settings.easingType, progress));
}

Engine::ParticlePendulumMovementModule::PendulumBasis Engine::ParticlePendulumMovementModule::CalculateBasis(
	const Particle& particle) const {

	const Vector3 axis = Vector3::NormalizeOr(
		particle.spawnDirection, Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 reference = std::abs(Vector3::Dot(axis, Vector3(0.0f, 1.0f, 0.0f))) < 0.999f ?
		Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
	const Vector3 basisX = Vector3::NormalizeOr(
		Vector3::Cross(reference, axis), Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 basisY = Vector3::NormalizeOr(
		Vector3::Cross(axis, basisX), Vector3(0.0f, 0.0f, 1.0f));
	return { axis, basisX, basisY };
}

float Engine::ParticlePendulumMovementModule::CalculateStartPhase(uint32_t particleID) const {

	const double phaseOffset = std::fmod(
		static_cast<double>(particlePhaseOffset_) * static_cast<double>(particleID), 360.0);
	return startPhase_ + static_cast<float>(phaseOffset);
}

Engine::Vector3 Engine::ParticlePendulumMovementModule::CalculateOffset(
	const PendulumBasis& basis, float particleStartPhase, float rawT) const {

	const float length = EvaluateAnimation(length_, rawT);
	const float maxAngle = EvaluateAnimation(maxAngle_, rawT);
	const float cycles = EvaluateAnimation(cycles_, rawT);
	const float planeAngle = Math::DegToRad(planeAngle_);
	const Vector3 swingDirection =
		basis.x * std::cos(planeAngle) + basis.y * std::sin(planeAngle);
	const float direction = reverse_ ? -1.0f : 1.0f;
	const float phase = Math::DegToRad(particleStartPhase + cycles * 360.0f * direction);
	const float swingAngle = Math::DegToRad(maxAngle * std::sin(phase));
	return swingDirection * (std::sin(swingAngle) * length) +
		basis.axis * ((1.0f - std::cos(swingAngle)) * length * arcStrength_);
}

#if defined(NEM_EDITOR_UI_ENABLED)
bool Engine::ParticlePendulumMovementModule::DrawAnimationSettings(
	const char* header, const char* id, const char* startLabel, const char* endLabel,
	FloatAnimationSettings& settings, float minValue, float maxValue) {

	if (!MyGUI::CollapsingHeader(header, false)) {
		return false;
	}

	ImGui::PushID(id);
	bool changed = false;
	changed |= MyGUI::DragFloat(
		startLabel, settings.start, ParticleGui::MakeDragSetting(minValue, maxValue)).valueChanged;
	changed |= MyGUI::DragFloat(
		endLabel, settings.end, ParticleGui::MakeDragSetting(minValue, maxValue)).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(settings.easingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useCurve);
	if (settings.useCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor(
			"Curve", settings.curve, settings.curveState, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			changed |= DrawCurveGenerator(
				settings.generatorState, GetCurveChannels(settings.curve), targets);
		}
	}
	changed |= ParticleGui::DrawLoopSettings(settings.loop);
	ImGui::PopID();
	return changed;
}
#endif
