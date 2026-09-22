#include "ParticleColorUVModuleDrawer.h"

#include <Engine/Editor/Tools/Builtin/Effect/GUI/ParticleGUIHelpers.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

#include <algorithm>
#include <span>

using namespace Engine;

namespace {

	constexpr size_t kVector2ChannelCount = 2;
}

bool ParticleColorUVModuleDrawer::Draw(IParticleModule& module) {

	auto* concrete = dynamic_cast<ParticleColorUVModule*>(&module);
	if (!concrete) {
		return false;
	}
	auto settings = concrete->GetSettings();

	bool changed = false;
	changed |= DrawOffsetSettings(settings);
	changed |= DrawScaleSettings(settings);
	changed |= DrawRotationSettings(settings);
	if (changed) {
		concrete->SetSettings(settings);
	}
	return changed;

}

bool ParticleColorUVModuleDrawer::DrawOffsetSettings(ParticleColorUVModule::Settings& settings) {

	if (!MyGUI::CollapsingHeader("座標", false)) {
		return false;
	}

	ImGui::PushID("Offset");
	bool changed = false;
	changed |= MyGUI::EnumCombo("更新方法", settings.updateType).valueChanged;
	if (settings.updateType == ParticleUVUpdateType::Scroll) {

		changed |= MyGUI::DragVector2("スクロール速度", settings.scrollSpeed, ParticleGUI::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
		ImGui::PopID();
		return changed;
	}
	changed |= MyGUI::DragVector2("開始座標", settings.startOffset, ParticleGUI::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector2("終了座標", settings.endOffset, ParticleGUI::MakeDragSetting(-100.0f, 100.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.offsetEasingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useOffsetCurve);
	if (settings.useOffsetCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		auto channels = std::span(settings.offsetCurve.channels.data(), kVector2ChannelCount);
		changed |= MyGUI::CurveEditor("OffsetCurve", channels, offsetCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XY", { 0u, 1u } }, { "X", { 0u } }, { "Y", { 1u } } };
			changed |= DrawCurveGenerator(offsetGeneratorState_, channels, targets);
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.offsetLoop);
	ImGui::PopID();
	return changed;
}

bool ParticleColorUVModuleDrawer::DrawScaleSettings(ParticleColorUVModule::Settings& settings) {

	if (!MyGUI::CollapsingHeader("スケール", false)) {
		return false;
	}

	ImGui::PushID("Scale");
	bool changed = false;
	changed |= MyGUI::DragVector2("開始スケール", settings.startScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= MyGUI::DragVector2("終了スケール", settings.endScale, ParticleGUI::MakeDragSetting(0.0f, 100.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.scaleEasingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useScaleCurve);
	if (settings.useScaleCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		auto channels = std::span(settings.scaleCurve.channels.data(), kVector2ChannelCount);
		changed |= MyGUI::CurveEditor("ScaleCurve", channels, scaleCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = {
				{ "XY", { 0u, 1u } }, { "X", { 0u } }, { "Y", { 1u } } };
			changed |= DrawCurveGenerator(scaleGeneratorState_, channels, targets);
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.scaleLoop);
	ImGui::PopID();
	return changed;
}

bool ParticleColorUVModuleDrawer::DrawRotationSettings(ParticleColorUVModule::Settings& settings) {

	if (!MyGUI::CollapsingHeader("回転", false)) {
		return false;
	}

	ImGui::PushID("Rotation");
	bool changed = false;
	changed |= MyGUI::DragVector2("UVピボット", settings.pivot, ParticleGUI::MakeDragSetting(-1.0f, 1.0f)).valueChanged;
	changed |= MyGUI::DragFloat("開始回転", settings.startRotation, ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
	changed |= MyGUI::DragFloat("終了回転", settings.endRotation, ParticleGUI::MakeDragSetting(-3600.0f, 3600.0f)).valueChanged;
	changed |= ParticleGUI::DrawInterpolationEasing(settings.rotationEasingType);
	changed |= MyGUI::Checkbox("カーブを使用", settings.useRotationCurve);
	if (settings.useRotationCurve) {

		CurveEditSetting setting{};
		setting.size = ImVec2(0.0f, 260.0f);
		setting.fixedTimeRange = true;
		changed |= MyGUI::CurveEditor("RotationCurve", settings.rotationCurve, rotationCurveState_, setting).valueChanged;

		if (MyGUI::CollapsingHeader("カーブ生成", false)) {

			static const CurveBakeTarget targets[] = { { "値", { 0u } } };
			changed |= DrawCurveGenerator(rotationGeneratorState_, GetCurveChannels(settings.rotationCurve), targets);
		}
	}
	changed |= ParticleGUI::DrawLoopSettings(settings.rotationLoop);
	ImGui::PopID();
	return changed;
}
