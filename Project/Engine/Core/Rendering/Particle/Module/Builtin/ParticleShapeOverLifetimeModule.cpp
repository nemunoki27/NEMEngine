#include "ParticleShapeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <array>
#include <span>

//============================================================================
//	ParticleShapeOverLifetimeModule internal
//============================================================================
namespace {

	constexpr const char* kRingOuterRadius = "ringOuterRadius";
	constexpr const char* kRingInnerRadius = "ringInnerRadius";
	constexpr const char* kRingStartAngle = "ringStartAngle";
	constexpr const char* kRingEndAngle = "ringEndAngle";

	constexpr const char* kCylinderTopRadius = "cylinderTopRadius";
	constexpr const char* kCylinderCenterRadius = "cylinderCenterRadius";
	constexpr const char* kCylinderBottomRadius = "cylinderBottomRadius";
	constexpr const char* kCylinderTopRadiusWeight = "cylinderTopRadiusWeight";
	constexpr const char* kCylinderBottomRadiusWeight = "cylinderBottomRadiusWeight";
	constexpr const char* kCylinderTopColor = "cylinderTopColor";
	constexpr const char* kCylinderCenterColor = "cylinderCenterColor";
	constexpr const char* kCylinderBottomColor = "cylinderBottomColor";
	constexpr const char* kCylinderHeight = "cylinderHeight";
	constexpr const char* kCylinderMaxAngle = "cylinderMaxAngle";

	enum ParameterCacheIndex : size_t {

		RingOuterRadius,
		RingInnerRadius,
		RingStartAngle,
		RingEndAngle,
		CylinderTopRadius,
		CylinderCenterRadius,
		CylinderBottomRadius,
		CylinderTopRadiusWeight,
		CylinderBottomRadiusWeight,
		CylinderTopColor,
		CylinderCenterColor,
		CylinderBottomColor,
		CylinderHeight,
		CylinderMaxAngle,
		ParameterCount,
	};
	static_assert(ParameterCount == 14);

	constexpr std::array<const char*, ParameterCount> kParameterNames = {
		kRingOuterRadius,
		kRingInnerRadius,
		kRingStartAngle,
		kRingEndAngle,
		kCylinderTopRadius,
		kCylinderCenterRadius,
		kCylinderBottomRadius,
		kCylinderTopRadiusWeight,
		kCylinderBottomRadiusWeight,
		kCylinderTopColor,
		kCylinderCenterColor,
		kCylinderBottomColor,
		kCylinderHeight,
		kCylinderMaxAngle,
	};

	Engine::Vector4 ToVector4(const Engine::Color4& color) {

		return Engine::Vector4(color.r, color.g, color.b, color.a);
	}

	Engine::ParticleMaterialAnimatedParameter MakeFloatParameter(float value) {

		Engine::ParticleMaterialAnimatedParameter parameter{};
		parameter.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		parameter.constant.x = value;
		parameter.start.x = value;
		parameter.end.x = value;
		parameter.componentCount = 1;
		return parameter;
	}

	Engine::ParticleMaterialAnimatedParameter MakeColorParameter(const Engine::Color4& value) {

		Engine::ParticleMaterialAnimatedParameter parameter{};
		parameter.mode = Engine::ParticleMaterialParameterMode::OverLifetime;
		parameter.constant = ToVector4(value);
		parameter.start = parameter.constant;
		parameter.end = parameter.constant;
		parameter.componentCount = 4;
		return parameter;
	}

	Engine::Color4 ToColor4(const Engine::Vector4& value) {

		return Engine::Color4(value.x, value.y, value.z, value.w);
	}
}

//============================================================================
//	ParticleShapeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleShapeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	parameters_.clear();
	const nlohmann::json safeParams = params.is_object() ? params : nlohmann::json::object();
	shape_ = EnumAdapter<PrimitiveType>::FromString(
		safeParams.value("shape", "Ring")).value_or(PrimitiveType::Ring);

	if (const auto it = safeParams.find("parameters"); it != safeParams.end() && it->is_object()) {
		for (auto parameterIt = it->begin(); parameterIt != it->end(); ++parameterIt) {

			ParticleMaterialAnimatedParameter parameter{};
			from_json(parameterIt.value(), parameter);
			parameters_[parameterIt.key()] = std::move(parameter);
		}
	} else {
		MigrateLegacyParameters(safeParams);
	}
	EnsureParameters();
}

nlohmann::json Engine::ParticleShapeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["shape"] = EnumAdapter<PrimitiveType>::ToString(shape_);
	params["parameters"] = nlohmann::json::object();
	for (const auto& [name, parameter] : parameters_) {
		to_json(params["parameters"][name], parameter);
	}
	return params;
}

bool Engine::ParticleShapeOverLifetimeModule::DrawImGui() {

	bool changed = false;
	ParticleParametricShapeRegistry& registry = ParticleParametricShapeRegistry::GetInstance();
	const std::vector<PrimitiveType> types = registry.GetTypes();

	int32_t currentIndex = 0;
	bool found = false;
	for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
		if (types[i] == shape_) { currentIndex = i; found = true; break; }
	}
	if (!found && !types.empty()) {

		shape_ = types.front();
		EnsureParameters();
		changed = true;
	}

	if (!types.empty() && MyGUI::BeginPropertyRow("対象形状")) {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Value", EnumAdapter<PrimitiveType>::ToString(types[currentIndex]))) {
			for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
				if (ImGui::Selectable(EnumAdapter<PrimitiveType>::ToString(types[i]), i == currentIndex)) {

					shape_ = types[i];
					EnsureParameters();
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}

	if (shape_ == PrimitiveType::Ring) {

		changed |= DrawParameter("外周半径", kRingOuterRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("内周半径", kRingInnerRadius, 0.5f, 0.0f, 10000.0f);
		changed |= DrawParameter("開始角", kRingStartAngle, 0.0f, 0.0f, 360.0f, 0.5f);
		changed |= DrawParameter("終了角", kRingEndAngle, 360.0f, 0.0f, 360.0f, 0.5f);
	} else if (shape_ == PrimitiveType::Cylinder) {

		changed |= DrawParameter("上面半径", kCylinderTopRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("中心半径", kCylinderCenterRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("下面半径", kCylinderBottomRadius, 1.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("上面Weight", kCylinderTopRadiusWeight, 0.0f, 0.0f, 1.0f);
		changed |= DrawParameter("下面Weight", kCylinderBottomRadiusWeight, 0.0f, 0.0f, 1.0f);
		changed |= DrawColorParameter("上面色", kCylinderTopColor, Color4::White());
		changed |= DrawColorParameter("中心色", kCylinderCenterColor, Color4::White());
		changed |= DrawColorParameter("底面色", kCylinderBottomColor, Color4::White());
		changed |= DrawParameter("高さ", kCylinderHeight, 2.0f, 0.0f, 10000.0f);
		changed |= DrawParameter("展開角", kCylinderMaxAngle, 360.0f, 0.0f, 360.0f, 0.5f);
	}
	return changed;
}

void Engine::ParticleShapeOverLifetimeModule::OnSpawn(Particle& particle) {

	EvaluateShape(particle, 0.0f);
}

void Engine::ParticleShapeOverLifetimeModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = particle.lifetime <= 0.0f ? 1.0f : particle.age / particle.lifetime;
	EvaluateShape(particle, progress);
}

void Engine::ParticleShapeOverLifetimeModule::EnsureParameters() {

	auto ensureFloat = [&](const char* key, float value) {

		auto it = parameters_.try_emplace(key, MakeFloatParameter(value)).first;
		it->second.componentCount = 1;
	};
	auto ensureColor = [&](const char* key, const Color4& value) {

		auto it = parameters_.try_emplace(key, MakeColorParameter(value)).first;
		it->second.componentCount = 4;
	};

	if (shape_ == PrimitiveType::Ring) {

		ensureFloat(kRingOuterRadius, 1.0f);
		ensureFloat(kRingInnerRadius, 0.5f);
		ensureFloat(kRingStartAngle, 0.0f);
		ensureFloat(kRingEndAngle, 360.0f);
	} else if (shape_ == PrimitiveType::Cylinder) {

		ensureFloat(kCylinderTopRadius, 1.0f);
		ensureFloat(kCylinderCenterRadius, 1.0f);
		ensureFloat(kCylinderBottomRadius, 1.0f);
		ensureFloat(kCylinderTopRadiusWeight, 0.0f);
		ensureFloat(kCylinderBottomRadiusWeight, 0.0f);
		ensureColor(kCylinderTopColor, Color4::White());
		ensureColor(kCylinderCenterColor, Color4::White());
		ensureColor(kCylinderBottomColor, Color4::White());
		ensureFloat(kCylinderHeight, 2.0f);
		ensureFloat(kCylinderMaxAngle, 360.0f);
	}
	RebuildParameterCache();
}

void Engine::ParticleShapeOverLifetimeModule::RebuildParameterCache() {

	parameterCache_.fill(nullptr);
	for (size_t index = 0; index < kParameterNames.size(); ++index) {

		const auto it = parameters_.find(kParameterNames[index]);
		if (it != parameters_.end()) {
			parameterCache_[index] = &it->second;
		}
	}
}

void Engine::ParticleShapeOverLifetimeModule::MigrateLegacyParameters(const nlohmann::json& params) {

	const EasingType easing = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	auto setFloat = [&](const char* key, float start, float end) {

		ParticleMaterialAnimatedParameter parameter = MakeFloatParameter(start);
		parameter.end.x = end;
		parameter.easingType = easing;
		parameters_[key] = std::move(parameter);
	};
	auto setColor = [&](const char* key, const Color4& start, const Color4& end) {

		ParticleMaterialAnimatedParameter parameter = MakeColorParameter(start);
		parameter.end = ToVector4(end);
		parameter.easingType = easing;
		parameters_[key] = std::move(parameter);
	};

	if (shape_ == PrimitiveType::Ring) {

		PrimitiveRingParams start{};
		PrimitiveRingParams end{};
		if (const auto it = params.find("ringStart"); it != params.end() && it->is_object()) { from_json(*it, start); }
		if (const auto it = params.find("ringEnd"); it != params.end() && it->is_object()) { from_json(*it, end); }
		setFloat(kRingOuterRadius, start.outerRadius, end.outerRadius);
		setFloat(kRingInnerRadius, start.innerRadius, end.innerRadius);
		setFloat(kRingStartAngle, start.startAngle, end.startAngle);
		setFloat(kRingEndAngle, start.endAngle, end.endAngle);
	} else if (shape_ == PrimitiveType::Cylinder) {

		PrimitiveCylinderParams start{};
		PrimitiveCylinderParams end{};
		if (const auto it = params.find("cylinderStart"); it != params.end() && it->is_object()) { from_json(*it, start); }
		if (const auto it = params.find("cylinderEnd"); it != params.end() && it->is_object()) { from_json(*it, end); }
		setFloat(kCylinderTopRadius, start.topRadius, end.topRadius);
		setFloat(kCylinderCenterRadius, start.centerRadius, end.centerRadius);
		setFloat(kCylinderBottomRadius, start.bottomRadius, end.bottomRadius);
		setFloat(kCylinderTopRadiusWeight, start.topRadiusWeight, end.topRadiusWeight);
		setFloat(kCylinderBottomRadiusWeight, start.bottomRadiusWeight, end.bottomRadiusWeight);
		setColor(kCylinderTopColor, start.topColor, end.topColor);
		setColor(kCylinderCenterColor, start.centerColor, end.centerColor);
		setColor(kCylinderBottomColor, start.bottomColor, end.bottomColor);
		setFloat(kCylinderHeight, start.height, end.height);
		setFloat(kCylinderMaxAngle, start.maxAngle, end.maxAngle);
	}
}

bool Engine::ParticleShapeOverLifetimeModule::DrawParameter(
	const char* label, const char* key, float defaultValue,
	float minValue, float maxValue, float dragSpeed) {

	auto [it, inserted] = parameters_.try_emplace(key, MakeFloatParameter(defaultValue));
	if (inserted) {
		RebuildParameterCache();
	}
	ParticleMaterialAnimatedParameter& parameter = it->second;
	parameter.componentCount = 1;

	bool changed = false;
	ImGui::PushID(key);
	if (MyGUI::CollapsingHeader(label, false)) {

		changed |= MyGUI::EnumCombo("更新方法", parameter.mode).valueChanged;
		const FloatEditSetting editSetting = ParticleGui::MakeDragSetting(minValue, maxValue, dragSpeed);
		if (parameter.mode == ParticleMaterialParameterMode::Constant) {

			changed |= MyGUI::DragFloat("定数", parameter.constant.x, editSetting).valueChanged;
		} else {

			changed |= MyGUI::DragFloat("開始", parameter.start.x, editSetting).valueChanged;
			changed |= MyGUI::DragFloat("終了", parameter.end.x, editSetting).valueChanged;
			changed |= ParticleGui::SelectEasing(parameter.easingType);
			changed |= MyGUI::Checkbox("カーブを使用", parameter.useCurve);
			if (parameter.useCurve) {

				CurveEditSetting setting{};
				setting.size = ImVec2(0.0f, 260.0f);
				setting.fixedTimeRange = true;
				ParameterUiState& uiState = uiStates_[key];
				changed |= MyGUI::CurveEditor("Curve", parameter.curveW,
					uiState.curveState, setting).valueChanged;
				if (MyGUI::CollapsingHeader("カーブ生成", false)) {

					static const CurveBakeTarget targets[] = { { "値", { 0u } } };
					changed |= DrawCurveGenerator(uiState.curveGeneratorState,
						GetCurveChannels(parameter.curveW), targets);
				}
			}
			changed |= ParticleGui::DrawLoopSettings(parameter.loop);
		}
	}
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleShapeOverLifetimeModule::DrawColorParameter(
	const char* label, const char* key, const Color4& defaultValue) {

	auto [it, inserted] = parameters_.try_emplace(key, MakeColorParameter(defaultValue));
	if (inserted) {
		RebuildParameterCache();
	}
	ParticleMaterialAnimatedParameter& parameter = it->second;
	parameter.componentCount = 4;

	bool changed = false;
	ImGui::PushID(key);
	if (MyGUI::CollapsingHeader(label, false)) {

		changed |= MyGUI::EnumCombo("更新方法", parameter.mode).valueChanged;
		auto drawColor = [&](const char* colorLabel, Vector4& value) {

			Color4 color = ToColor4(value);
			if (MyGUI::ColorEdit(colorLabel, color).valueChanged) {

				value = ToVector4(color);
				return true;
			}
			return false;
		};
		if (parameter.mode == ParticleMaterialParameterMode::Constant) {

			changed |= drawColor("定数", parameter.constant);
		} else {

			changed |= drawColor("開始", parameter.start);
			changed |= drawColor("終了", parameter.end);
			changed |= ParticleGui::SelectEasing(parameter.easingType);
			changed |= MyGUI::Checkbox("カーブを使用", parameter.useCurve);
			if (parameter.useCurve) {

				std::array<CurveChannelRef, 4> refs = {
					CurveChannelRef{ &parameter.curve3.channels[0], "R" },
					CurveChannelRef{ &parameter.curve3.channels[1], "G" },
					CurveChannelRef{ &parameter.curve3.channels[2], "B" },
					CurveChannelRef{ &parameter.curveW.channel, "A" },
				};
				CurveEditSetting setting{};
				setting.size = ImVec2(0.0f, 260.0f);
				setting.fixedTimeRange = true;
				ParameterUiState& uiState = uiStates_[key];
				changed |= MyGUI::CurveEditor("Curve", std::span(refs),
					uiState.curveState, setting).valueChanged;
				if (MyGUI::CollapsingHeader("カーブ生成", false)) {

					static const CurveBakeTarget colorTargets[] = {
						{ "RGB", { 0u, 1u, 2u } }, { "R", { 0u } },
						{ "G", { 1u } }, { "B", { 2u } } };
					changed |= DrawCurveGenerator(uiState.curveGeneratorState,
						parameter.curve3.channels, colorTargets);

					ImGui::SeparatorText("A");
					static const CurveBakeTarget alphaTargets[] = { { "値", { 0u } } };
					changed |= DrawCurveGenerator(uiState.alphaGeneratorState,
						GetCurveChannels(parameter.curveW), alphaTargets);
				}
			}
			changed |= ParticleGui::DrawLoopSettings(parameter.loop);
		}
	}
	ImGui::PopID();
	return changed;
}

void Engine::ParticleShapeOverLifetimeModule::EvaluateShape(
	Particle& particle, float progress) const {

	auto evaluate = [&](size_t index, const Vector4& defaultValue) {

		const ParticleMaterialAnimatedParameter* parameter = parameterCache_[index];
		return parameter ?
			EvaluateParticleMaterialParameter(*parameter, progress) : defaultValue;
	};

	particle.shapeData = ParticleShapeData{};
	if (shape_ == PrimitiveType::Ring) {

		const float outerRadius = (std::max)(evaluate(RingOuterRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
		const float innerRadius = (std::max)(evaluate(RingInnerRadius, Vector4(0.5f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
		const float startAngle = std::clamp(evaluate(RingStartAngle, Vector4{}).x, 0.0f, 360.0f);
		const float endAngle = std::clamp(evaluate(RingEndAngle, Vector4(360.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f, 360.0f);
		particle.shapeData.params0 = Vector4(
			outerRadius, innerRadius, startAngle * Math::radian, endAngle * Math::radian);
		return;
	}
	if (shape_ != PrimitiveType::Cylinder) {
		return;
	}

	const float topRadius = (std::max)(evaluate(CylinderTopRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float centerRadius = (std::max)(evaluate(CylinderCenterRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float bottomRadius = (std::max)(evaluate(CylinderBottomRadius, Vector4(1.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float topWeight = std::clamp(evaluate(CylinderTopRadiusWeight, Vector4{}).x, 0.0f, 1.0f);
	const float bottomWeight = std::clamp(evaluate(CylinderBottomRadiusWeight, Vector4{}).x, 0.0f, 1.0f);
	const float height = (std::max)(evaluate(CylinderHeight, Vector4(2.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f);
	const float maxAngle = std::clamp(
		evaluate(CylinderMaxAngle, Vector4(360.0f, 0.0f, 0.0f, 0.0f)).x, 0.0f, 360.0f);

	particle.shapeData.params0 = Vector4(topRadius, centerRadius, bottomRadius, height);
	particle.shapeData.params1 = Vector4(topWeight, bottomWeight, maxAngle * Math::radian, 1.0f);
	particle.shapeData.topColor = ToColor4(evaluate(CylinderTopColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
	particle.shapeData.centerColor = ToColor4(evaluate(CylinderCenterColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
	particle.shapeData.bottomColor = ToColor4(evaluate(CylinderBottomColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f)));
}
