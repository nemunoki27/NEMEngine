#include "ParticleRotationModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleRotationModule internal
//============================================================================
namespace {

	// Vector3カーブを読み込む
	void ReadCurveChannels(const nlohmann::json& params, const char* key, Engine::CurveVector3& curve) {

		const auto it = params.find(key);
		if (it == params.end() || !it->is_array()) {
			return;
		}
		const size_t count = (std::min)(curve.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], curve.channels[i]);
		}
	}

	// Vector3カーブを書き出す
	nlohmann::json WriteCurveChannels(const Engine::CurveVector3& curve) {

		nlohmann::json channels = nlohmann::json::array();
		for (const Engine::CurveChannel& channel : curve.channels) {
			channels.push_back(channel);
		}
		return channels;
	}

	// Quaternionカーブを読み込む
	void ReadQuaternionCurve(const nlohmann::json& params, const char* channelKey,
		const char* axisKeyName, Engine::CurveQuaternion& curve) {

		if (const auto it = params.find(channelKey);
			it != params.end() && it->is_array()) {

			const size_t count = (std::min)(curve.channels.size(), it->size());
			for (size_t i = 0; i < count; ++i) {
				from_json((*it)[i], curve.channels[i]);
			}
		}
		curve.axisKeys.clear();
		if (const auto it = params.find(axisKeyName);
			it != params.end() && it->is_array()) {

			for (const nlohmann::json& axisJson : *it) {

				if (!axisJson.is_object()) { continue; }
				Engine::CurveQuaternionAxisKey axisSetting = Engine::QuaternionAxisKeyUtility::MakeDefault();
				axisSetting.useCustomAxis = axisJson.value("useCustomAxis", axisSetting.useCustomAxis);
				if (const auto axis = axisJson.find("customAxis"); axis != axisJson.end()) {
					axisSetting.customAxis = Engine::Vector3::FromJson(*axis);
				}
				axisSetting.axes.clear();
				if (const auto axes = axisJson.find("axes"); axes != axisJson.end() && axes->is_array()) {
					for (const nlohmann::json& value : *axes) {

						if (!value.is_string()) { continue; }
						const auto axis = Engine::EnumAdapter<Engine::Axis>::FromString(value.get<std::string>());
						if (axis) { axisSetting.axes.emplace_back(*axis); }
					}
				}
				curve.axisKeys.emplace_back(Engine::QuaternionAxisKeyUtility::Sanitize(axisSetting));
			}
		}
		curve.EnsureAxisKeyCount();
	}

	// Quaternionカーブの軸キーを書き出す
	nlohmann::json WriteQuaternionAxisKeys(const Engine::CurveQuaternion& curve) {

		nlohmann::json axisKeys = nlohmann::json::array();
		for (const Engine::CurveQuaternionAxisKey& axisKey : curve.axisKeys) {

			nlohmann::json axisJson = nlohmann::json::object();
			axisJson["useCustomAxis"] = axisKey.useCustomAxis;
			axisJson["customAxis"] = axisKey.customAxis.ToJson();
			axisJson["axes"] = nlohmann::json::array();
			for (Engine::Axis axis : axisKey.axes) {
				axisJson["axes"].push_back(Engine::EnumAdapter<Engine::Axis>::ToString(axis));
			}
			axisKeys.push_back(std::move(axisJson));
		}
		return axisKeys;
	}

	// AxisとAngleからQuaternionを作る
	Engine::Quaternion MakeAxisRotation(const Engine::Vector3& axis, float angleDegrees) {

		Engine::CurveQuaternionAxisKey axisKey{};
		axisKey.useCustomAxis = true;
		axisKey.customAxis = axis;
		return Engine::Quaternion::Normalize(Engine::Quaternion::MakeAxisAngle(
			Engine::QuaternionAxisKeyUtility::GetAxisDirection(axisKey),
			Math::DegToRad(angleDegrees)));
	}

	// 正規化した回転軸を取得する
	Engine::Vector3 GetAxisDirection(const Engine::Vector3& axis) {

		Engine::CurveQuaternionAxisKey axisKey{};
		axisKey.useCustomAxis = true;
		axisKey.customAxis = axis;
		return Engine::QuaternionAxisKeyUtility::GetAxisDirection(axisKey);
	}

	// 角速度ベクトルをクォータニオンへ積分する
	void AddRotation(Engine::Particle& particle, const Engine::Vector3& rotationSpeed, float deltaTime) {

		const float speed = Engine::Vector3::Length(rotationSpeed);
		if (speed <= 0.0f) {
			return;
		}
		const Engine::Vector3 axis = rotationSpeed * (1.0f / speed);
		particle.rotation = Engine::Quaternion::Normalize(Engine::Quaternion::MakeAxisAngle(
			axis, speed * Math::radian * deltaTime) * particle.rotation);
	}

}

//============================================================================
//	ParticleRotationModule classMethods
//============================================================================
void Engine::ParticleRotationModule::FromJson(const nlohmann::json& params) {

	settings_.mode = EnumAdapter<ParticleRotationMode>::FromString(
		params.value("mode", "Fixed")).value_or(ParticleRotationMode::Fixed);
	settings_.valueType = EnumAdapter<ParticleRotationValueType>::FromString(
		params.value("valueType", "Euler")).value_or(ParticleRotationValueType::Euler);
	if (const auto it = params.find("fixedAngle"); it != params.end()) { from_json(*it, settings_.fixedAngle); }
	if (const auto it = params.find("fixedAxis"); it != params.end()) { settings_.fixedAxis = Vector3::FromJson(*it); }
	if (const auto it = params.find("fixedQuaternionAngle"); it != params.end()) {
		from_json(*it, settings_.fixedQuaternionAngle);
	}
	if (const auto it = params.find("addAngle"); it != params.end()) { from_json(*it, settings_.addAngle); }
	if (const auto it = params.find("addAxis"); it != params.end()) { settings_.addAxis = Vector3::FromJson(*it); }
	if (const auto it = params.find("addQuaternionAngle"); it != params.end()) {
		from_json(*it, settings_.addQuaternionAngle);
	}
	settings_.speedMode = EnumAdapter<ParticleRotationSpeedMode>::FromString(
		params.value("speedMode", "Constant")).value_or(ParticleRotationSpeedMode::Constant);
	if (const auto it = params.find("rotationSpeed"); it != params.end()) { from_json(*it, settings_.rotationSpeed); }
	if (const auto it = params.find("rotationSpeedAxis"); it != params.end()) {
		settings_.rotationSpeedAxis = Vector3::FromJson(*it);
	}
	if (const auto it = params.find("rotationQuaternionSpeed"); it != params.end()) {
		from_json(*it, settings_.rotationQuaternionSpeed);
	}
	if (const auto it = params.find("startSpeed"); it != params.end()) { settings_.startSpeed = Vector3::FromJson(*it); }
	if (const auto it = params.find("endSpeed"); it != params.end()) { settings_.endSpeed = Vector3::FromJson(*it); }
	if (const auto it = params.find("startSpeedAxis"); it != params.end()) { settings_.startSpeedAxis = Vector3::FromJson(*it); }
	if (const auto it = params.find("endSpeedAxis"); it != params.end()) { settings_.endSpeedAxis = Vector3::FromJson(*it); }
	settings_.startQuaternionSpeed = params.value("startQuaternionSpeed", settings_.startQuaternionSpeed);
	settings_.endQuaternionSpeed = params.value("endQuaternionSpeed", settings_.endQuaternionSpeed);
	settings_.speedEasingType = EnumAdapter<EasingType>::FromString(
		params.value("speedEasingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("speedLoop"); it != params.end()) { from_json(*it, settings_.speedLoop); }
	settings_.useSpeedCurve = params.value("useSpeedCurve", settings_.useSpeedCurve);
	ReadCurveChannels(params, "speedCurveChannels", settings_.speedCurve);
	ReadQuaternionCurve(params, "speedQuaternionCurveChannels",
		"speedQuaternionCurveAxisKeys", settings_.speedQuaternionCurve);

	if (const auto it = params.find("startAngle"); it != params.end()) { settings_.startAngle = Vector3::FromJson(*it); }
	if (const auto it = params.find("endAngle"); it != params.end()) { settings_.endAngle = Vector3::FromJson(*it); }
	if (const auto it = params.find("startAxis"); it != params.end()) { settings_.startAxis = Vector3::FromJson(*it); }
	if (const auto it = params.find("endAxis"); it != params.end()) { settings_.endAxis = Vector3::FromJson(*it); }
	settings_.startQuaternionAngle = params.value("startQuaternionAngle", settings_.startQuaternionAngle);
	settings_.endQuaternionAngle = params.value("endQuaternionAngle", settings_.endQuaternionAngle);
	settings_.angleEasingType = EnumAdapter<EasingType>::FromString(
		params.value("angleEasingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("angleLoop"); it != params.end()) { from_json(*it, settings_.angleLoop); }
	settings_.useAngleCurve = params.value("useAngleCurve", settings_.useAngleCurve);
	ReadCurveChannels(params, "angleCurveChannels", settings_.angleCurve);
	ReadQuaternionCurve(params, "quaternionCurveChannels",
		"quaternionCurveAxisKeys", settings_.quaternionCurve);
}

nlohmann::json Engine::ParticleRotationModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["mode"] = EnumAdapter<ParticleRotationMode>::ToString(settings_.mode);
	params["valueType"] = EnumAdapter<ParticleRotationValueType>::ToString(settings_.valueType);
	to_json(params["fixedAngle"], settings_.fixedAngle);
	params["fixedAxis"] = settings_.fixedAxis.ToJson();
	to_json(params["fixedQuaternionAngle"], settings_.fixedQuaternionAngle);
	to_json(params["addAngle"], settings_.addAngle);
	params["addAxis"] = settings_.addAxis.ToJson();
	to_json(params["addQuaternionAngle"], settings_.addQuaternionAngle);
	params["speedMode"] = EnumAdapter<ParticleRotationSpeedMode>::ToString(settings_.speedMode);
	to_json(params["rotationSpeed"], settings_.rotationSpeed);
	params["rotationSpeedAxis"] = settings_.rotationSpeedAxis.ToJson();
	to_json(params["rotationQuaternionSpeed"], settings_.rotationQuaternionSpeed);
	params["startSpeed"] = settings_.startSpeed.ToJson();
	params["endSpeed"] = settings_.endSpeed.ToJson();
	params["startSpeedAxis"] = settings_.startSpeedAxis.ToJson();
	params["endSpeedAxis"] = settings_.endSpeedAxis.ToJson();
	params["startQuaternionSpeed"] = settings_.startQuaternionSpeed;
	params["endQuaternionSpeed"] = settings_.endQuaternionSpeed;
	params["speedEasingType"] = EnumAdapter<EasingType>::ToString(settings_.speedEasingType);
	to_json(params["speedLoop"], settings_.speedLoop);
	params["useSpeedCurve"] = settings_.useSpeedCurve;
	params["speedCurveChannels"] = WriteCurveChannels(settings_.speedCurve);
	params["speedQuaternionCurveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : settings_.speedQuaternionCurve.channels) {
		params["speedQuaternionCurveChannels"].push_back(channel);
	}
	params["speedQuaternionCurveAxisKeys"] = WriteQuaternionAxisKeys(settings_.speedQuaternionCurve);
	params["startAngle"] = settings_.startAngle.ToJson();
	params["endAngle"] = settings_.endAngle.ToJson();
	params["startAxis"] = settings_.startAxis.ToJson();
	params["endAxis"] = settings_.endAxis.ToJson();
	params["startQuaternionAngle"] = settings_.startQuaternionAngle;
	params["endQuaternionAngle"] = settings_.endQuaternionAngle;
	params["angleEasingType"] = EnumAdapter<EasingType>::ToString(settings_.angleEasingType);
	to_json(params["angleLoop"], settings_.angleLoop);
	params["useAngleCurve"] = settings_.useAngleCurve;
	params["angleCurveChannels"] = WriteCurveChannels(settings_.angleCurve);
	params["quaternionCurveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : settings_.quaternionCurve.channels) {
		params["quaternionCurveChannels"].push_back(channel);
	}
	params["quaternionCurveAxisKeys"] = WriteQuaternionAxisKeys(settings_.quaternionCurve);
	return params;
}

void Engine::ParticleRotationModule::OnSpawn(Particle& particle) {

	switch (settings_.mode) {
	case ParticleRotationMode::Fixed:
		particle.rotation = settings_.valueType == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(settings_.fixedAngle.Sample()) :
			MakeAxisRotation(settings_.fixedAxis, settings_.fixedQuaternionAngle.Sample());
		break;
	case ParticleRotationMode::Additive:
		particle.rotation = Quaternion::Normalize((settings_.valueType == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(settings_.addAngle.Sample()) :
			MakeAxisRotation(settings_.addAxis, settings_.addQuaternionAngle.Sample())) * particle.rotation);
		particle.rotationSpeed = Vector3::AnyInit(0.0f);
		if (settings_.speedMode == ParticleRotationSpeedMode::Constant) {
			particle.rotationSpeed = settings_.valueType == ParticleRotationValueType::Euler ?
				settings_.rotationSpeed.Sample() :
				GetAxisDirection(settings_.rotationSpeedAxis) * settings_.rotationQuaternionSpeed.Sample();
		}
		break;
	case ParticleRotationMode::Interpolate:
		particle.rotation = settings_.valueType == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(settings_.startAngle) :
			MakeAxisRotation(settings_.startAxis, settings_.startQuaternionAngle);
		break;
	}
}

void Engine::ParticleRotationModule::OnUpdate(Particle& particle, float deltaTime) {

	if (settings_.mode == ParticleRotationMode::Fixed) {
		return;
	}
	if (settings_.mode == ParticleRotationMode::Additive) {

		Vector3 speed = particle.rotationSpeed;
		if (settings_.speedMode == ParticleRotationSpeedMode::OverLifetime) {

			const float progress = settings_.speedLoop.LoopedT(particle.age / particle.lifetime);
			if (settings_.valueType == ParticleRotationValueType::Euler) {
				speed = settings_.useSpeedCurve ? settings_.speedCurve.Evaluate(progress) :
					Vector3::Lerp(settings_.startSpeed, settings_.endSpeed, EasedValue(settings_.speedEasingType, progress));
			} else if (settings_.useSpeedCurve) {
				speed = settings_.speedQuaternionCurve.EvaluateAxis(progress) *
					settings_.speedQuaternionCurve.EvaluateAngle(progress);
			} else {

				const float easedT = EasedValue(settings_.speedEasingType, progress);
				const Vector3 axis = GetAxisDirection(Vector3::Lerp(settings_.startSpeedAxis, settings_.endSpeedAxis, easedT));
				speed = axis * std::lerp(settings_.startQuaternionSpeed, settings_.endQuaternionSpeed, easedT);
			}
		}
		AddRotation(particle, speed, deltaTime);
		return;
	}

	const float progress = settings_.angleLoop.LoopedT(particle.age / particle.lifetime);
	if (settings_.valueType == ParticleRotationValueType::Quaternion) {

		if (settings_.useAngleCurve) {
			particle.rotation = settings_.quaternionCurve.Evaluate(progress);
		} else {

			const Quaternion start = MakeAxisRotation(settings_.startAxis, settings_.startQuaternionAngle);
			const Quaternion end = MakeAxisRotation(settings_.endAxis, settings_.endQuaternionAngle);
			particle.rotation = Quaternion::Lerp(start, end, EasedValue(settings_.angleEasingType, progress));
		}
		return;
	}

	const Vector3 angle = settings_.useAngleCurve ? settings_.angleCurve.Evaluate(progress) :
		Vector3::Lerp(settings_.startAngle, settings_.endAngle, EasedValue(settings_.angleEasingType, progress));
	particle.rotation = Quaternion::FromEulerDegrees(angle);
}
