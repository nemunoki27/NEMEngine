#include "ParticleColorOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleColorOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleColorOverLifetimeModule::FromJson(const nlohmann::json& params) {

	if (const auto it = params.find("startColor"); it != params.end()) {
		startColor_ = Color4::FromJson(*it);
	}
	if (const auto it = params.find("endColor"); it != params.end()) {
		endColor_ = Color4::FromJson(*it);
	}
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	useCurve_ = params.value("useCurve", useCurve_);
	if (const auto it = params.find("curveChannels"); it != params.end() && it->is_array()) {

		const size_t count = (std::min)(curve_.channels.size(), it->size());
		for (size_t i = 0; i < count; ++i) {
			from_json((*it)[i], curve_.channels[i]);
		}
	}
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, loop_); }
}

nlohmann::json Engine::ParticleColorOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startColor"] = startColor_.ToJson();
	params["endColor"] = endColor_.ToJson();
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	params["useCurve"] = useCurve_;
	params["curveChannels"] = nlohmann::json::array();
	for (const CurveChannel& channel : curve_.channels) {
		params["curveChannels"].push_back(channel);
	}
	to_json(params["loop"], loop_);
	return params;
}

void Engine::ParticleColorOverLifetimeModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = loop_.LoopedT(particle.age / particle.lifetime);
	// カーブ指定があればカーブを優先し、無ければイージング補間する
	const Color4 color = useCurve_ ? curve_.Evaluate(progress) :
		Color4::Lerp(startColor_, endColor_, EasedValue(easingType_, progress));
	particle.color = color;
}

bool Engine::ParticleColorOverLifetimeModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= MyGUI::ColorEdit("開始色", startColor_).valueChanged;
	changed |= MyGUI::ColorEdit("終了色", endColor_).valueChanged;
	changed |= ParticleGui::SelectEasing(easingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useCurve_);
	if (useCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("ColorCurve", curve_, curveState_, setting).valueChanged;
		// 可視時間範囲の色遷移を帯で表示する
		MyGUI::CurveColorGradientBar(curve_.channels, curveState_.visibleTimeMin, curveState_.visibleTimeMax, true);

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "RGB", { 0u, 1u, 2u } }, { "R", { 0u } }, { "G", { 1u } }, { "B", { 2u } }, { "A", { 3u } } };
			if (DrawCurveGenerator(generatorState_, curve_.channels, targets)) {
				changed = true;
			}
		}
	}
	changed |= ParticleGui::DrawLoopSettings(loop_);
	return changed;
#else
	return false;
#endif
}
