#include "ParticleScaleOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleScaleOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleScaleOverLifetimeModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startScale"); it != params.end()) { startScale_ = Vector3::FromJson(*it); }
	if (const auto it = params.find("endScale"); it != params.end()) { endScale_ = Vector3::FromJson(*it); }
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, loop_); }
	useCurve_ = params.value("useCurve", useCurve_);
	if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

		const size_t count = (std::min)(curve_.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], curve_.channels[i]);
		}
	}
}

nlohmann::json Engine::ParticleScaleOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = startScale_.ToJson();
	params["endScale"] = endScale_.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	to_json(params["loop"], loop_);
	params["useCurve"] = useCurve_;
	params["curveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : curve_.channels) {
		params["curveChannels"].push_back(channel);
	}
	return params;
}

void Engine::ParticleScaleOverLifetimeModule::OnUpdate(std::span<Particle> alive, [[maybe_unused]] float deltaTime) {

	for (Particle& particle : alive) {

		const float progress = loop_.LoopedT(particle.age / particle.lifetime);
		// カーブ指定があればカーブを優先し、無ければイージング補間する
		particle.scale = useCurve_ ? curve_.Evaluate(progress) :
			Vector3::Lerp(startScale_, endScale_, EasedValue(easingType_, progress));
	}
}

bool Engine::ParticleScaleOverLifetimeModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragVector3("開始スケール", startScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector3("終了スケール", endScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::SelectEasing(easingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useCurve_);
	if (useCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("ScaleCurve", curve_, curveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XYZ", { 0u, 1u, 2u } }, { "X", { 0u } }, { "Y", { 1u } }, { "Z", { 2u } } };
			if (DrawCurveGenerator(generatorState_, curve_.channels, targets)) {
				changed = true;
			}
		}
	}
	changed |= ParticleGui::DrawLoopSettings(loop_);
	return changed;
}
