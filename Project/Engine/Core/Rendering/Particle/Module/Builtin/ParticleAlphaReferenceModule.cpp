#include "ParticleAlphaReferenceModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleAlphaReferenceModule classMethods
//============================================================================
void Engine::ParticleAlphaReferenceModule::FromJson(const nlohmann::json& params) {

	startReference_ = params.value("startReference", startReference_);
	endReference_ = params.value("endReference", endReference_);
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
}

nlohmann::json Engine::ParticleAlphaReferenceModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startReference"] = startReference_;
	params["endReference"] = endReference_;
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	return params;
}

void Engine::ParticleAlphaReferenceModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
	particle.alphaReference = Math::Lerp(startReference_, endReference_, EasedValue(easingType_, progress));
}

bool Engine::ParticleAlphaReferenceModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= MyGUI::DragFloat("開始閾値", startReference_, ParticleGui::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	changed |= MyGUI::DragFloat("終了閾値", endReference_, ParticleGui::MakeDragSetting(0.0f, 1.0f, 0.005f)).valueChanged;
	changed |= ParticleGui::SelectEasing(easingType_);
	return changed;
#else
	return false;
#endif
}
