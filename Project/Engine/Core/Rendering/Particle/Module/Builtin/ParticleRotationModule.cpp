#include "ParticleRotationModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleRotationModule internal
//============================================================================
namespace {

	// 数値ならZ軸のみの旧スキーマとして読む
	void ReadLegacyAxisValue(const nlohmann::json& params, const char* key, Engine::Vector3& out) {

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

	// 旧ランダム範囲をParticleValueへ変換する
	Engine::ParticleValue<Engine::Vector3> MakeLegacyValue(
		const Engine::Vector3& minValue, const Engine::Vector3& maxValue) {

		Engine::ParticleValue<Engine::Vector3> value{};
		value.type = minValue == maxValue ?
			Engine::ParticleValueType::Constant : Engine::ParticleValueType::Random;
		value.constant = minValue;
		value.min = minValue;
		value.max = maxValue;
		return value;
	}

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

	// 回転モードを日本語で選択する
	bool DrawRotationMode(Engine::ParticleRotationMode& mode) {

		const char* labels[] = { "固定角度", "角度加算", "角度補間" };
		int32_t current = static_cast<int32_t>(mode);
		if (!Engine::MyGUI::BeginPropertyRow("回転モード")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			mode = static_cast<Engine::ParticleRotationMode>(current);
		}
		return changed;
	}

	// 回転速度モードを日本語で選択する
	bool DrawRotationSpeedMode(Engine::ParticleRotationSpeedMode& mode) {

		const char* labels[] = { "定数", "オーバーライフタイム" };
		int32_t current = static_cast<int32_t>(mode);
		if (!Engine::MyGUI::BeginPropertyRow("回転速度")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			mode = static_cast<Engine::ParticleRotationSpeedMode>(current);
		}
		return changed;
	}

	// 角度の入力形式を日本語で選択する
	bool DrawRotationValueType(Engine::ParticleRotationValueType& type) {

		const char* labels[] = { "オイラー角", "Quaternion" };
		int32_t current = static_cast<int32_t>(type);
		if (!Engine::MyGUI::BeginPropertyRow("角度形式")) {
			return false;
		}
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		const bool changed = ImGui::Combo("##Value", &current, labels, IM_ARRAYSIZE(labels));
		Engine::MyGUI::EndPropertyRow();
		if (changed) {
			type = static_cast<Engine::ParticleRotationValueType>(current);
		}
		return changed;
	}

	// ランダム範囲を持つAxisとAngleを編集する
	bool DrawAxisAngleValue(const char* label, Engine::Vector3& axis,
		Engine::ParticleValue<float>& angle) {

		ImGui::PushID(label);
		ImGui::SeparatorText(label);
		bool changed = Engine::MyGUI::DragVector3("Axis", axis,
			Engine::ParticleGui::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= Engine::ParticleGui::DrawParticleValueFloat("Angle", angle,
			Engine::ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		ImGui::PopID();
		return changed;
	}

	// AxisとAngleを編集する
	bool DrawAxisAngle(const char* label, Engine::Vector3& axis, float& angle) {

		ImGui::PushID(label);
		ImGui::SeparatorText(label);
		bool changed = Engine::MyGUI::DragVector3("Axis", axis,
			Engine::ParticleGui::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
		changed |= Engine::MyGUI::DragFloat("Angle", angle,
			Engine::ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		ImGui::PopID();
		return changed;
	}
}

//============================================================================
//	ParticleRotationModule classMethods
//============================================================================
void Engine::ParticleRotationModule::FromJson(const nlohmann::json& params) {

	// RotationOverLifetimeの旧スキーマは角度加算へ移行する
	if (!params.contains("mode")) {

		Vector3 initialMin = Vector3::AnyInit(0.0f);
		Vector3 initialMax = Vector3(0.0f, 0.0f, 360.0f);
		Vector3 speedMin = Vector3(0.0f, 0.0f, -90.0f);
		Vector3 speedMax = Vector3(0.0f, 0.0f, 90.0f);
		ReadLegacyAxisValue(params, "initialMin", initialMin);
		ReadLegacyAxisValue(params, "initialMax", initialMax);
		ReadLegacyAxisValue(params, "speedMin", speedMin);
		ReadLegacyAxisValue(params, "speedMax", speedMax);
		mode_ = ParticleRotationMode::Additive;
		valueType_ = ParticleRotationValueType::Euler;
		speedMode_ = ParticleRotationSpeedMode::Constant;
		addAngle_ = MakeLegacyValue(initialMin, initialMax);
		rotationSpeed_ = MakeLegacyValue(speedMin, speedMax);
		return;
	}

	mode_ = EnumAdapter<ParticleRotationMode>::FromString(
		params.value("mode", "Fixed")).value_or(ParticleRotationMode::Fixed);
	valueType_ = EnumAdapter<ParticleRotationValueType>::FromString(
		params.value("valueType", "Euler")).value_or(ParticleRotationValueType::Euler);
	if (const auto it = params.find("fixedAngle"); it != params.end()) { from_json(*it, fixedAngle_); }
	if (const auto it = params.find("fixedAxis"); it != params.end()) { fixedAxis_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("fixedQuaternionAngle"); it != params.end()) {
		from_json(*it, fixedQuaternionAngle_);
	}
	if (const auto it = params.find("addAngle"); it != params.end()) { from_json(*it, addAngle_); }
	if (const auto it = params.find("addAxis"); it != params.end()) { addAxis_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("addQuaternionAngle"); it != params.end()) {
		from_json(*it, addQuaternionAngle_);
	}
	speedMode_ = EnumAdapter<ParticleRotationSpeedMode>::FromString(
		params.value("speedMode", "Constant")).value_or(ParticleRotationSpeedMode::Constant);
	if (const auto it = params.find("rotationSpeed"); it != params.end()) { from_json(*it, rotationSpeed_); }
	if (const auto it = params.find("rotationSpeedAxis"); it != params.end()) {
		rotationSpeedAxis_ = Vector3::FromJson(*it);
	}
	if (const auto it = params.find("rotationQuaternionSpeed"); it != params.end()) {
		from_json(*it, rotationQuaternionSpeed_);
	}
	if (const auto it = params.find("startSpeed"); it != params.end()) { startSpeed_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endSpeed"); it != params.end()) { endSpeed_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("startSpeedAxis"); it != params.end()) { startSpeedAxis_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endSpeedAxis"); it != params.end()) { endSpeedAxis_ = Vector3::FromJson(*it); }
	startQuaternionSpeed_ = params.value("startQuaternionSpeed", startQuaternionSpeed_);
	endQuaternionSpeed_ = params.value("endQuaternionSpeed", endQuaternionSpeed_);
	speedEasingType_ = EnumAdapter<EasingType>::FromString(
		params.value("speedEasingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("speedLoop"); it != params.end()) { from_json(*it, speedLoop_); }
	useSpeedCurve_ = params.value("useSpeedCurve", useSpeedCurve_);
	ReadCurveChannels(params, "speedCurveChannels", speedCurve_);
	ReadQuaternionCurve(params, "speedQuaternionCurveChannels",
		"speedQuaternionCurveAxisKeys", speedQuaternionCurve_);

	if (const auto it = params.find("startAngle"); it != params.end()) { startAngle_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endAngle"); it != params.end()) { endAngle_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("startAxis"); it != params.end()) { startAxis_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endAxis"); it != params.end()) { endAxis_ = Vector3::FromJson(*it); }
	startQuaternionAngle_ = params.value("startQuaternionAngle", startQuaternionAngle_);
	endQuaternionAngle_ = params.value("endQuaternionAngle", endQuaternionAngle_);
	angleEasingType_ = EnumAdapter<EasingType>::FromString(
		params.value("angleEasingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("angleLoop"); it != params.end()) { from_json(*it, angleLoop_); }
	useAngleCurve_ = params.value("useAngleCurve", useAngleCurve_);
	ReadCurveChannels(params, "angleCurveChannels", angleCurve_);
	ReadQuaternionCurve(params, "quaternionCurveChannels",
		"quaternionCurveAxisKeys", quaternionCurve_);
}

nlohmann::json Engine::ParticleRotationModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["mode"] = EnumAdapter<ParticleRotationMode>::ToString(mode_);
	params["valueType"] = EnumAdapter<ParticleRotationValueType>::ToString(valueType_);
	to_json(params["fixedAngle"], fixedAngle_);
	params["fixedAxis"] = fixedAxis_.ToJson();
	to_json(params["fixedQuaternionAngle"], fixedQuaternionAngle_);
	to_json(params["addAngle"], addAngle_);
	params["addAxis"] = addAxis_.ToJson();
	to_json(params["addQuaternionAngle"], addQuaternionAngle_);
	params["speedMode"] = EnumAdapter<ParticleRotationSpeedMode>::ToString(speedMode_);
	to_json(params["rotationSpeed"], rotationSpeed_);
	params["rotationSpeedAxis"] = rotationSpeedAxis_.ToJson();
	to_json(params["rotationQuaternionSpeed"], rotationQuaternionSpeed_);
	params["startSpeed"] = startSpeed_.ToJson();
	params["endSpeed"] = endSpeed_.ToJson();
	params["startSpeedAxis"] = startSpeedAxis_.ToJson();
	params["endSpeedAxis"] = endSpeedAxis_.ToJson();
	params["startQuaternionSpeed"] = startQuaternionSpeed_;
	params["endQuaternionSpeed"] = endQuaternionSpeed_;
	params["speedEasingType"] = EnumAdapter<EasingType>::ToString(speedEasingType_);
	to_json(params["speedLoop"], speedLoop_);
	params["useSpeedCurve"] = useSpeedCurve_;
	params["speedCurveChannels"] = WriteCurveChannels(speedCurve_);
	params["speedQuaternionCurveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : speedQuaternionCurve_.channels) {
		params["speedQuaternionCurveChannels"].push_back(channel);
	}
	params["speedQuaternionCurveAxisKeys"] = WriteQuaternionAxisKeys(speedQuaternionCurve_);
	params["startAngle"] = startAngle_.ToJson();
	params["endAngle"] = endAngle_.ToJson();
	params["startAxis"] = startAxis_.ToJson();
	params["endAxis"] = endAxis_.ToJson();
	params["startQuaternionAngle"] = startQuaternionAngle_;
	params["endQuaternionAngle"] = endQuaternionAngle_;
	params["angleEasingType"] = EnumAdapter<EasingType>::ToString(angleEasingType_);
	to_json(params["angleLoop"], angleLoop_);
	params["useAngleCurve"] = useAngleCurve_;
	params["angleCurveChannels"] = WriteCurveChannels(angleCurve_);
	params["quaternionCurveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : quaternionCurve_.channels) {
		params["quaternionCurveChannels"].push_back(channel);
	}
	params["quaternionCurveAxisKeys"] = WriteQuaternionAxisKeys(quaternionCurve_);
	return params;
}

void Engine::ParticleRotationModule::OnSpawn(Particle& particle) {

	switch (mode_) {
	case ParticleRotationMode::Fixed:
		particle.rotation = valueType_ == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(fixedAngle_.Sample()) :
			MakeAxisRotation(fixedAxis_, fixedQuaternionAngle_.Sample());
		break;
	case ParticleRotationMode::Additive:
		particle.rotation = Quaternion::Normalize((valueType_ == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(addAngle_.Sample()) :
			MakeAxisRotation(addAxis_, addQuaternionAngle_.Sample())) * particle.rotation);
		particle.rotationSpeed = Vector3::AnyInit(0.0f);
		if (speedMode_ == ParticleRotationSpeedMode::Constant) {
			particle.rotationSpeed = valueType_ == ParticleRotationValueType::Euler ?
				rotationSpeed_.Sample() :
				GetAxisDirection(rotationSpeedAxis_) * rotationQuaternionSpeed_.Sample();
		}
		break;
	case ParticleRotationMode::Interpolate:
		particle.rotation = valueType_ == ParticleRotationValueType::Euler ?
			Quaternion::FromEulerDegrees(startAngle_) :
			MakeAxisRotation(startAxis_, startQuaternionAngle_);
		break;
	}
}

void Engine::ParticleRotationModule::OnUpdate(Particle& particle, float deltaTime) {

	if (mode_ == ParticleRotationMode::Fixed) {
		return;
	}
	if (mode_ == ParticleRotationMode::Additive) {

		Vector3 speed = particle.rotationSpeed;
		if (speedMode_ == ParticleRotationSpeedMode::OverLifetime) {

			const float progress = speedLoop_.LoopedT(particle.age / particle.lifetime);
			if (valueType_ == ParticleRotationValueType::Euler) {
				speed = useSpeedCurve_ ? speedCurve_.Evaluate(progress) :
					Vector3::Lerp(startSpeed_, endSpeed_, EasedValue(speedEasingType_, progress));
			} else if (useSpeedCurve_) {
				speed = speedQuaternionCurve_.EvaluateAxis(progress) *
					speedQuaternionCurve_.EvaluateAngle(progress);
			} else {

				const float easedT = EasedValue(speedEasingType_, progress);
				const Vector3 axis = GetAxisDirection(Vector3::Lerp(startSpeedAxis_, endSpeedAxis_, easedT));
				speed = axis * std::lerp(startQuaternionSpeed_, endQuaternionSpeed_, easedT);
			}
		}
		AddRotation(particle, speed, deltaTime);
		return;
	}

	const float progress = angleLoop_.LoopedT(particle.age / particle.lifetime);
	if (valueType_ == ParticleRotationValueType::Quaternion) {

		if (useAngleCurve_) {
			particle.rotation = quaternionCurve_.Evaluate(progress);
		} else {

			const Quaternion start = MakeAxisRotation(startAxis_, startQuaternionAngle_);
			const Quaternion end = MakeAxisRotation(endAxis_, endQuaternionAngle_);
			particle.rotation = Quaternion::Lerp(start, end, EasedValue(angleEasingType_, progress));
		}
		return;
	}

	const Vector3 angle = useAngleCurve_ ? angleCurve_.Evaluate(progress) :
		Vector3::Lerp(startAngle_, endAngle_, EasedValue(angleEasingType_, progress));
	particle.rotation = Quaternion::FromEulerDegrees(angle);
}

bool Engine::ParticleRotationModule::DrawImGui() {

	bool changed = DrawRotationMode(mode_);
	changed |= DrawRotationValueType(valueType_);
	if (mode_ == ParticleRotationMode::Fixed) {

		if (valueType_ == ParticleRotationValueType::Euler) {
			changed |= ParticleGui::DrawParticleValueVector3("固定角度", fixedAngle_,
				ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		} else {
			changed |= DrawAxisAngleValue("固定角度", fixedAxis_, fixedQuaternionAngle_);
		}
	} else if (mode_ == ParticleRotationMode::Additive) {
		changed |= DrawAdditiveSettings();
	} else {
		changed |= DrawInterpolationSettings();
	}
	return changed;
}

bool Engine::ParticleRotationModule::DrawAdditiveSettings() {

	bool changed = false;
	if (valueType_ == ParticleRotationValueType::Euler) {
		changed |= ParticleGui::DrawParticleValueVector3("加算角度", addAngle_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
	} else {
		changed |= DrawAxisAngleValue("加算角度", addAxis_, addQuaternionAngle_);
	}
	changed |= DrawRotationSpeedMode(speedMode_);
	if (speedMode_ == ParticleRotationSpeedMode::Constant) {

		if (valueType_ == ParticleRotationValueType::Euler) {
			changed |= ParticleGui::DrawParticleValueVector3("回転速度", rotationSpeed_,
				ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f));
		} else {
			changed |= DrawAxisAngleValue(
				"回転速度", rotationSpeedAxis_, rotationQuaternionSpeed_);
		}
		return changed;
	}

	if (valueType_ == ParticleRotationValueType::Euler) {

		changed |= MyGUI::DragVector3("開始回転速度", startSpeed_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		changed |= MyGUI::DragVector3("終了回転速度", endSpeed_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	} else {

		changed |= DrawAxisAngle("開始回転速度", startSpeedAxis_, startQuaternionSpeed_);
		changed |= DrawAxisAngle("終了回転速度", endSpeedAxis_, endQuaternionSpeed_);
	}
	changed |= ParticleGui::SelectEasing(speedEasingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useSpeedCurve_);
	if (useSpeedCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		if (valueType_ == ParticleRotationValueType::Euler) {
			changed |= MyGUI::CurveEditor("RotationSpeedCurve", speedCurve_, speedCurveState_, setting).valueChanged;
		} else {
			changed |= MyGUI::CurveEditor("RotationSpeedQuaternionCurve",
				speedQuaternionCurve_, speedQuaternionCurveState_, setting).valueChanged;
		}

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			if (valueType_ == ParticleRotationValueType::Euler) {

				static const CurveBakeTarget targets[] = {
					{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
				changed |= DrawCurveGenerator(speedGeneratorState_, speedCurve_.channels, targets);
			} else {

				static const CurveBakeTarget targets[] = { { "Angle", { 1u } } };
				changed |= DrawCurveGenerator(speedQuaternionGeneratorState_,
					speedQuaternionCurve_.channels, targets);
			}
		}
	}
	changed |= ParticleGui::DrawLoopSettings(speedLoop_);
	return changed;
}

bool Engine::ParticleRotationModule::DrawInterpolationSettings() {

	bool changed = false;
	if (valueType_ == ParticleRotationValueType::Euler) {

		changed |= MyGUI::DragVector3("開始角度", startAngle_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
		changed |= MyGUI::DragVector3("終了角度", endAngle_,
			ParticleGui::MakeDragSetting(-3600.0f, 3600.0f, 0.5f)).valueChanged;
	} else {

		changed |= DrawAxisAngle("開始角度", startAxis_, startQuaternionAngle_);
		changed |= DrawAxisAngle("終了角度", endAxis_, endQuaternionAngle_);
	}
	changed |= ParticleGui::SelectEasing(angleEasingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useAngleCurve_);
	if (useAngleCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		if (valueType_ == ParticleRotationValueType::Euler) {
			changed |= MyGUI::CurveEditor("RotationAngleCurve", angleCurve_, angleCurveState_, setting).valueChanged;
		} else {
			changed |= MyGUI::CurveEditor(
				"RotationQuaternionCurve", quaternionCurve_, quaternionCurveState_, setting).valueChanged;
		}

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			if (valueType_ == ParticleRotationValueType::Euler) {

				static const CurveBakeTarget targets[] = {
					{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
				changed |= DrawCurveGenerator(angleGeneratorState_, angleCurve_.channels, targets);
			} else {

				static const CurveBakeTarget targets[] = { { "Angle", { 1u } } };
				changed |= DrawCurveGenerator(
					quaternionGeneratorState_, quaternionCurve_.channels, targets);
			}
		}
	}
	changed |= ParticleGui::DrawLoopSettings(angleLoop_);
	return changed;
}
