#include "ParticleSpiralMovementModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <cmath>
#include <span>

//============================================================================
//	ParticleSpiralMovementModule classMethods
//============================================================================
void Engine::ParticleSpiralMovementModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("radius"); it != params.end()) {
		ReadAnimationSettings(*it, radius_);
	}
	if (const auto it = params.find("turns"); it != params.end()) {
		ReadAnimationSettings(*it, turns_);
	}
	startAngle_ = params.value("startAngle", startAngle_);
	particleAngleOffset_ = params.value("particleAngleOffset", particleAngleOffset_);
	reverse_ = params.value("reverse", reverse_);
}

nlohmann::json Engine::ParticleSpiralMovementModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["radius"] = WriteAnimationSettings(radius_);
	params["turns"] = WriteAnimationSettings(turns_);
	params["startAngle"] = startAngle_;
	params["particleAngleOffset"] = particleAngleOffset_;
	params["reverse"] = reverse_;
	return params;
}

void Engine::ParticleSpiralMovementModule::OnSpawn(Particle& particle) {

	const SpiralBasis basis = CalculateBasis(particle);
	particle.pos += CalculateOffset(basis, CalculateStartAngle(particle.id), 0.0f);
}

void Engine::ParticleSpiralMovementModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float currentT = particle.age / particle.lifetime;
	const SpiralBasis basis = CalculateBasis(particle);
	const float particleStartAngle = CalculateStartAngle(particle.id);
	if (particle.phaseIndex != particle.previousPhaseIndex) {

		particle.pos += CalculateOffset(basis, particleStartAngle, currentT);
		return;
	}

	const float previousT = particle.previousAge / particle.lifetime;
	particle.pos += CalculateOffset(basis, particleStartAngle, currentT) -
		CalculateOffset(basis, particleStartAngle, previousT);
}

bool Engine::ParticleSpiralMovementModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= DrawAnimationSettings(
		"渦半径", "Radius", "開始半径", "終了半径", radius_, 0.0f, 10000.0f);
	changed |= DrawAnimationSettings(
		"渦巻き回数", "Turns", "開始回転数", "終了回転数", turns_, 0.0f, 1000.0f);

	if (MyGUI::CollapsingHeader("角度", false)) {

		changed |= MyGUI::DragFloat(
			"開始角度", startAngle_, ParticleGui::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
		changed |= MyGUI::DragFloat(
			"粒子ごとの角度差", particleAngleOffset_,
			ParticleGui::MakeDragSetting(-360.0f, 360.0f)).valueChanged;
		changed |= MyGUI::Checkbox("逆回転", reverse_);
	}
	return changed;
#else
	return false;
#endif
}

void Engine::ParticleSpiralMovementModule::ReadAnimationSettings(
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

nlohmann::json Engine::ParticleSpiralMovementModule::WriteAnimationSettings(
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

float Engine::ParticleSpiralMovementModule::EvaluateAnimation(
	const FloatAnimationSettings& settings, float rawT) const {

	const float progress = settings.loop.LoopedT(rawT);
	return settings.useCurve ? settings.curve.Evaluate(progress) :
		Math::Lerp(settings.start, settings.end, EasedValue(settings.easingType, progress));
}

Engine::ParticleSpiralMovementModule::SpiralBasis Engine::ParticleSpiralMovementModule::CalculateBasis(
	const Particle& particle) const {

	const Vector3 axis = Vector3::NormalizeOr(
		particle.spawnDirection, Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 reference = std::abs(Vector3::Dot(axis, Vector3(0.0f, 1.0f, 0.0f))) < 0.999f ?
		Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
	const Vector3 basisX = Vector3::NormalizeOr(
		Vector3::Cross(reference, axis), Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 basisY = Vector3::NormalizeOr(
		Vector3::Cross(axis, basisX), Vector3(0.0f, 0.0f, 1.0f));
	return { basisX, basisY };
}

float Engine::ParticleSpiralMovementModule::CalculateStartAngle(uint32_t particleID) const {

	const double angleOffset = std::fmod(
		static_cast<double>(particleAngleOffset_) * static_cast<double>(particleID), 360.0);
	return startAngle_ + static_cast<float>(angleOffset);
}

Engine::Vector3 Engine::ParticleSpiralMovementModule::CalculateOffset(
	const SpiralBasis& basis, float particleStartAngle, float rawT) const {

	const float radius = EvaluateAnimation(radius_, rawT);
	const float turns = EvaluateAnimation(turns_, rawT);
	const float direction = reverse_ ? -1.0f : 1.0f;
	const float angle = Math::DegToRad(particleStartAngle + turns * 360.0f * direction);
	return (basis.x * std::cos(angle) + basis.y * std::sin(angle)) * radius;
}

#if defined(NEM_EDITOR_UI_ENABLED)
bool Engine::ParticleSpiralMovementModule::DrawAnimationSettings(
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
	changed |= ParticleGui::SelectEasing(settings.easingType);
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
