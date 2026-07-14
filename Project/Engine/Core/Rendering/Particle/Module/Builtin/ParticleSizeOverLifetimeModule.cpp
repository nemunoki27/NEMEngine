#include "ParticleSizeOverLifetimeModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleSizeOverLifetimeModule classMethods
//============================================================================
void Engine::ParticleSizeOverLifetimeModule::FromJson(const nlohmann::json& params) {

	startScale_ = params.value("startScale", startScale_);
	endScale_ = params.value("endScale", endScale_);
	easingType_ = EnumAdapter<EasingType>::FromString(
		params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	useCurve_ = params.value("useCurve", useCurve_);
	if (const auto it = params.find("curve"); it != params.end() && it->is_object()) {
		from_json(*it, curve_.channel);
	}
	if (const auto it = params.find("loop"); it != params.end()) { from_json(*it, loop_); }
}

nlohmann::json Engine::ParticleSizeOverLifetimeModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["startScale"] = startScale_;
	params["endScale"] = endScale_;
	params["easingType"] = EnumAdapter<EasingType>::ToString(easingType_);
	params["useCurve"] = useCurve_;
	params["curve"] = curve_.channel;
	to_json(params["loop"], loop_);
	return params;
}

void Engine::ParticleSizeOverLifetimeModule::OnUpdate(
	Particle& particle, [[maybe_unused]] float deltaTime) {

	const float progress = loop_.LoopedT(particle.age / particle.lifetime);
	// カーブ指定があればカーブを優先し、無ければイージング補間する
	const float scale = useCurve_ ? curve_.Evaluate(progress) :
		Math::Lerp(startScale_, endScale_, EasedValue(easingType_, progress));
	particle.size = scale;
}

bool Engine::ParticleSizeOverLifetimeModule::DrawImGui() {

	bool changed = false;
	changed |= MyGUI::DragFloat("開始倍率", startScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了倍率", endScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::SelectEasing(easingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useCurve_);
	if (useCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		// 進行度のカーブなので時間軸を0~1で固定する
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("SizeCurve", curve_, curveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			if (DrawCurveGenerator(generatorState_, std::span<CurveChannel>(&curve_.channel, 1), targets)) {
				changed = true;
			}
		}
	}
	changed |= ParticleGui::DrawLoopSettings(loop_);
	return changed;
}
