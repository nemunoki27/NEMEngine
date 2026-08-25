#include "ParticleColorUVModule.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <span>

//============================================================================
//	ParticleColorUVModule internal
//============================================================================
namespace {

	constexpr size_t kVector2ChannelCount = 2;

	// カーブチャンネルを読み込む
	void ReadCurveChannels(const nlohmann::json& in, std::span<Engine::CurveChannel> channels) {

		if (!in.is_array()) {
			return;
		}
		const size_t count = (std::min)(channels.size(), in.size());
		for (size_t i = 0; i < count; ++i) {
			from_json(in[i], channels[i]);
		}
	}

	// カーブチャンネルを書き出す
	nlohmann::json WriteCurveChannels(std::span<const Engine::CurveChannel> channels) {

		nlohmann::json out = nlohmann::json::array();
		for (const Engine::CurveChannel& channel : channels) {
			out.push_back(channel);
		}
		return out;
	}

	// Vector2アニメーション設定を読み込む
	void ReadVector2Animation(const nlohmann::json& in,
		Engine::Vector2& start, Engine::Vector2& end, EasingType& easingType,
		Engine::ParticleLoopSettings& loop, bool& useCurve, Engine::CurveVector3& curve) {

		if (!in.is_object()) {
			return;
		}
		if (const auto it = in.find("start"); it != in.end()) { start = Engine::Vector2::FromJson(*it); }
		if (const auto it = in.find("end"); it != in.end()) { end = Engine::Vector2::FromJson(*it); }
		easingType = Engine::EnumAdapter<EasingType>::FromString(
			in.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		if (const auto it = in.find("loop"); it != in.end()) { from_json(*it, loop); }
		useCurve = in.value("useCurve", useCurve);
		if (const auto it = in.find("curveChannels"); it != in.end()) {
			ReadCurveChannels(*it, std::span(curve.channels.data(), kVector2ChannelCount));
		}
	}

	// Vector2アニメーション設定を書き出す
	nlohmann::json WriteVector2Animation(const Engine::Vector2& start, const Engine::Vector2& end,
		EasingType easingType, const Engine::ParticleLoopSettings& loop,
		bool useCurve, const Engine::CurveVector3& curve) {

		nlohmann::json out = nlohmann::json::object();
		out["start"] = start.ToJson();
		out["end"] = end.ToJson();
		out["easingType"] = Engine::EnumAdapter<EasingType>::ToString(easingType);
		to_json(out["loop"], loop);
		out["useCurve"] = useCurve;
		out["curveChannels"] = WriteCurveChannels(
			std::span(curve.channels.data(), kVector2ChannelCount));
		return out;
	}

	// floatアニメーション設定を読み込む
	void ReadFloatAnimation(const nlohmann::json& in,
		float& start, float& end, EasingType& easingType,
		Engine::ParticleLoopSettings& loop, bool& useCurve, Engine::CurveFloat& curve) {

		if (!in.is_object()) {
			return;
		}
		start = in.value("start", start);
		end = in.value("end", end);
		easingType = Engine::EnumAdapter<EasingType>::FromString(
			in.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
		if (const auto it = in.find("loop"); it != in.end()) { from_json(*it, loop); }
		useCurve = in.value("useCurve", useCurve);
		if (const auto it = in.find("curve"); it != in.end() && it->is_object()) {
			from_json(*it, curve.channel);
		}
	}

	// floatアニメーション設定を書き出す
	nlohmann::json WriteFloatAnimation(float start, float end, EasingType easingType,
		const Engine::ParticleLoopSettings& loop, bool useCurve, const Engine::CurveFloat& curve) {

		nlohmann::json out = nlohmann::json::object();
		out["start"] = start;
		out["end"] = end;
		out["easingType"] = Engine::EnumAdapter<EasingType>::ToString(easingType);
		to_json(out["loop"], loop);
		out["useCurve"] = useCurve;
		to_json(out["curve"], curve.channel);
		return out;
	}
}

//============================================================================
//	ParticleColorUVModule classMethods
//============================================================================
void Engine::ParticleColorUVModule::FromJson(const nlohmann::json& params) {

	const auto offsetIt = params.find("offset");
	if (offsetIt != params.end() && offsetIt->is_object()) {
		updateType_ = EnumAdapter<ParticleUVUpdateType>::FromString(
			offsetIt->value("updateType", "Lerp")).value_or(ParticleUVUpdateType::Lerp);
		if (const auto it = offsetIt->find("scrollSpeed"); it != offsetIt->end()) {
			scrollSpeed_ = Vector2::FromJson(*it);
		}
		ReadVector2Animation(*offsetIt, startOffset_, endOffset_, offsetEasingType_,
			offsetLoop_, useOffsetCurve_, offsetCurve_);
	} else {
		updateType_ = EnumAdapter<ParticleUVUpdateType>::FromString(
			params.value("updateType", "Lerp")).value_or(ParticleUVUpdateType::Lerp);
		if (const auto it = params.find("startOffset"); it != params.end()) { startOffset_ = Vector2::FromJson(*it); }
		if (const auto it = params.find("endOffset"); it != params.end()) { endOffset_ = Vector2::FromJson(*it); }
		if (const auto it = params.find("scrollSpeed"); it != params.end()) { scrollSpeed_ = Vector2::FromJson(*it); }
		offsetEasingType_ = EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	}

	const auto scaleIt = params.find("scale");
	if (scaleIt != params.end() && scaleIt->is_object()) {
		ReadVector2Animation(*scaleIt, startScale_, endScale_, scaleEasingType_,
			scaleLoop_, useScaleCurve_, scaleCurve_);
	} else {
		if (const auto it = params.find("startScale"); it != params.end()) { startScale_ = Vector2::FromJson(*it); }
		if (const auto it = params.find("endScale"); it != params.end()) { endScale_ = Vector2::FromJson(*it); }
		scaleEasingType_ = EnumAdapter<EasingType>::FromString(
			params.value("easingType", "EaseOutSine")).value_or(EasingType::EaseOutSine);
	}

	if (const auto it = params.find("rotation"); it != params.end() && it->is_object()) {
		ReadFloatAnimation(*it, startRotation_, endRotation_, rotationEasingType_,
			rotationLoop_, useRotationCurve_, rotationCurve_);
		if (const auto pivotIt = it->find("pivot"); pivotIt != it->end()) {
			pivot_ = Vector2::FromJson(*pivotIt);
		}
	}
}

nlohmann::json Engine::ParticleColorUVModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["offset"] = WriteVector2Animation(startOffset_, endOffset_, offsetEasingType_,
		offsetLoop_, useOffsetCurve_, offsetCurve_);
	params["offset"]["updateType"] = EnumAdapter<ParticleUVUpdateType>::ToString(updateType_);
	params["offset"]["scrollSpeed"] = scrollSpeed_.ToJson();
	params["scale"] = WriteVector2Animation(startScale_, endScale_, scaleEasingType_,
		scaleLoop_, useScaleCurve_, scaleCurve_);
	params["rotation"] = WriteFloatAnimation(startRotation_, endRotation_, rotationEasingType_,
		rotationLoop_, useRotationCurve_, rotationCurve_);
	params["rotation"]["pivot"] = pivot_.ToJson();
	return params;
}

void Engine::ParticleColorUVModule::OnUpdate(Particle& particle, float deltaTime) {

	const float lifetimeT = particle.age / particle.lifetime;
	const float scaleT = scaleLoop_.LoopedT(lifetimeT);
	const float rotationT = rotationLoop_.LoopedT(lifetimeT);

	if (updateType_ == ParticleUVUpdateType::Scroll) {
		particle.uvOffset += scrollSpeed_ * deltaTime;
	} else {

		const float offsetT = offsetLoop_.LoopedT(lifetimeT);
		if (useOffsetCurve_) {
			particle.uvOffset = Vector2(
				offsetCurve_.channels[0].Evaluate(offsetT),
				offsetCurve_.channels[1].Evaluate(offsetT));
		} else {
			particle.uvOffset = Vector2::Lerp(startOffset_, endOffset_, EasedValue(offsetEasingType_, offsetT));
		}
	}

	if (useScaleCurve_) {
		particle.uvScale = Vector2(
			scaleCurve_.channels[0].Evaluate(scaleT),
			scaleCurve_.channels[1].Evaluate(scaleT));
	} else {
		particle.uvScale = Vector2::Lerp(startScale_, endScale_, EasedValue(scaleEasingType_, scaleT));
	}

	particle.uvRotation = useRotationCurve_ ? rotationCurve_.Evaluate(rotationT) :
		std::lerp(startRotation_, endRotation_, EasedValue(rotationEasingType_, rotationT));
	particle.uvPivot = pivot_;
}

bool Engine::ParticleColorUVModule::DrawImGui() {
#if defined(NEM_EDITOR_UI_ENABLED)

	bool changed = false;
	changed |= DrawOffsetSettings();
	changed |= DrawScaleSettings();
	changed |= DrawRotationSettings();
	return changed;
#else
	return false;
#endif
}

#if defined(NEM_EDITOR_UI_ENABLED)
bool Engine::ParticleColorUVModule::DrawOffsetSettings() {

	if (!MyGUI::CollapsingHeader("座標", false)) {
		return false;
	}

	ImGui::PushID("Offset");
	bool changed = false;
	changed |= MyGUI::EnumCombo("更新方法", updateType_).valueChanged;
	if (updateType_ == ParticleUVUpdateType::Scroll) {

		changed |= MyGUI::DragVector2("スクロール速度", scrollSpeed_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		ImGui::PopID();
		return changed;
	}
	changed |= MyGUI::DragVector2("開始座標", startOffset_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector2("終了座標", endOffset_, ParticleGui::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(offsetEasingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useOffsetCurve_);
	if (useOffsetCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		auto channels = std::span(offsetCurve_.channels.data(), kVector2ChannelCount);
		changed |= MyGUI::CurveEditor("OffsetCurve", channels, offsetCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XY", { 0u, 1u } }, { "X", { 0u } }, { "Y", { 1u } } };
			changed |= DrawCurveGenerator(offsetGeneratorState_, channels, targets);
		}
	}
	changed |= ParticleGui::DrawLoopSettings(offsetLoop_);
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleColorUVModule::DrawScaleSettings() {

	if (!MyGUI::CollapsingHeader("スケール", false)) {
		return false;
	}

	ImGui::PushID("Scale");
	bool changed = false;
	changed |= MyGUI::DragVector2("開始スケール", startScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector2("終了スケール", endScale_, ParticleGui::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(scaleEasingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useScaleCurve_);
	if (useScaleCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		auto channels = std::span(scaleCurve_.channels.data(), kVector2ChannelCount);
		changed |= MyGUI::CurveEditor("ScaleCurve", channels, scaleCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XY", { 0u, 1u } }, { "X", { 0u } }, { "Y", { 1u } } };
			changed |= DrawCurveGenerator(scaleGeneratorState_, channels, targets);
		}
	}
	changed |= ParticleGui::DrawLoopSettings(scaleLoop_);
	ImGui::PopID();
	return changed;
}

bool Engine::ParticleColorUVModule::DrawRotationSettings() {

	if (!MyGUI::CollapsingHeader("回転", false)) {
		return false;
	}

	ImGui::PushID("Rotation");
	bool changed = false;
	changed |= MyGUI::DragVector2("UVピボット", pivot_, ParticleGui::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
	changed |= MyGUI::DragFloat("開始回転", startRotation_, ParticleGui::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了回転", endRotation_, ParticleGui::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
	changed |= ParticleGui::DrawInterpolationEasing(rotationEasingType_);
	changed |= MyGUI::Checkbox("カーブを使用", useRotationCurve_);
	if (useRotationCurve_) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("RotationCurve", rotationCurve_, rotationCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			changed |= DrawCurveGenerator(rotationGeneratorState_, GetCurveChannels(rotationCurve_), targets);
		}
	}
	changed |= ParticleGui::DrawLoopSettings(rotationLoop_);
	ImGui::PopID();
	return changed;
}
#endif
