#include "ParticleShapeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleShapeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleShapeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	params_ = params.is_object() ? params : nlohmann::json::object();
	shape_ = EnumAdapter<PrimitiveType>::FromString(
		params_.value("shape", "Ring")).value_or(PrimitiveType::Ring);
	easingType_ = EnumAdapter<EasingType>::FromString(
		params_.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	PackShapeParams();
}

nlohmann::json Engine::ParticleShapeOverLifetimeModule::ToJson() const {

	nlohmann::json params = params_;
	params["shape"] = EnumAdapter<PrimitiveType>::ToString(shape_);
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	return params;
}

bool Engine::ParticleShapeOverLifetimeModule::DrawImGui() {

	bool changed = false;
	ParticleParametricShapeRegistry& registry = ParticleParametricShapeRegistry::GetInstance();
	const std::vector<PrimitiveType> types = registry.GetTypes();

	// 対象形状の選択、未登録の形状は先頭へフォールバック
	int32_t currentIndex = 0;
	bool found = false;
	for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
		if (types[i] == shape_) { currentIndex = i; found = true; break; }
	}
	if (!found && !types.empty()) {

		shape_ = types.front();
		changed = true;
	}

	if (!types.empty() && MyGUI::BeginPropertyRow("対象形状")) {

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##Value", EnumAdapter<PrimitiveType>::ToString(types[currentIndex]))) {
			for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
				if (ImGui::Selectable(EnumAdapter<PrimitiveType>::ToString(types[i]), i == currentIndex)) {

					shape_ = types[i];
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}

	// 形状別パラメータ
	if (const IParticleParametricShape* parametric = registry.Find(shape_)) {
		changed |= parametric->DrawImGui(params_);
	}
	changed |= ParticleGui::SelectEasing(easingType_);

	if (changed) {
		PackShapeParams();
	}
	return changed;
}

void Engine::ParticleShapeOverLifetimeModule::OnSpawn(Particle& particle) {

	particle.shapeParams = shapeStart_;
}

void Engine::ParticleShapeOverLifetimeModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
	const float easedT = EasedValue(easingType_, progress);
	particle.shapeParams = Vector4(
		Math::Lerp(shapeStart_.x, shapeEnd_.x, easedT),
		Math::Lerp(shapeStart_.y, shapeEnd_.y, easedT),
		Math::Lerp(shapeStart_.z, shapeEnd_.z, easedT),
		Math::Lerp(shapeStart_.w, shapeEnd_.w, easedT));
}

void Engine::ParticleShapeOverLifetimeModule::PackShapeParams() {

	shapeStart_ = Vector4{};
	shapeEnd_ = Vector4{};
	if (const IParticleParametricShape* parametric = ParticleParametricShapeRegistry::GetInstance().Find(shape_)) {
		parametric->PackShapeParams(params_, shapeStart_, shapeEnd_);
	}
}
