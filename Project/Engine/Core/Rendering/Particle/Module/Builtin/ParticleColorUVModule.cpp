#include "ParticleColorUVModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleColorUVModule classMethods
//============================================================================
void Engine::ParticleColorUVModule::FromJson(const nlohmann::json& params) {

	updateType_ = EnumAdapter<ParticleUVUpdateType>::FromString(
		params.value("updateType", "Lerp")).value_or(ParticleUVUpdateType::Lerp);
	if (const auto it = params.find("startOffset"); it != params.end()) { startOffset_ = Vector2::FromJson(*it); }
	if (const auto it = params.find("endOffset"); it != params.end()) { endOffset_ = Vector2::FromJson(*it); }
	if (const auto it = params.find("startScale"); it != params.end()) { startScale_ = Vector2::FromJson(*it); }
	if (const auto it = params.find("endScale"); it != params.end()) { endScale_ = Vector2::FromJson(*it); }
	if (const auto it = params.find("scrollSpeed"); it != params.end()) { scrollSpeed_ = Vector2::FromJson(*it); }
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
}

nlohmann::json Engine::ParticleColorUVModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["updateType"] = EnumAdapter<ParticleUVUpdateType>::ToString(updateType_);
	params["startOffset"] = startOffset_.ToJson();
	params["endOffset"] = endOffset_.ToJson();
	params["startScale"] = startScale_.ToJson();
	params["endScale"] = endScale_.ToJson();
	params["scrollSpeed"] = scrollSpeed_.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	return params;
}

void Engine::ParticleColorUVModule::OnUpdate(std::span<Particle> alive, float deltaTime) {

	for (Particle& particle : alive) {

		if (updateType_ == ParticleUVUpdateType::Lerp) {

			const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
			const float easedT = EasedValue(easingType_, progress);
			particle.uvOffset = Vector2::Lerp(startOffset_, endOffset_, easedT);
			particle.uvScale = Vector2::Lerp(startScale_, endScale_, easedT);
		} else {

			particle.uvOffset += scrollSpeed_ * deltaTime;
		}
	}
}

bool Engine::ParticleColorUVModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::EnumCombo("更新方法", updateType_).valueChanged;
	if (updateType_ == ParticleUVUpdateType::Lerp) {

		changed |= MyGUI::DragVector2("開始オフセット", startOffset_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		changed |= MyGUI::DragVector2("終了オフセット", endOffset_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		changed |= MyGUI::DragVector2("開始スケール", startScale_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		changed |= MyGUI::DragVector2("終了スケール", endScale_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		changed |= ParticleGui::SelectEasing(easingType_);
	} else {

		changed |= MyGUI::DragVector2("スクロール速度", scrollSpeed_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
	}
	return changed;
}
