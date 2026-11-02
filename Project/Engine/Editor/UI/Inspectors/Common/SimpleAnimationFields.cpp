#include "SimpleAnimationFields.h"

//============================================================================
//	include
//============================================================================
#include "InspectorDrawerCommon.h"
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <array>
#include <algorithm>
#include <format>
#include <cmath>
#include <type_traits>

namespace {

	// 回転軸は角度がゼロでも編集を続けられるよう入力中の値を保持する
	Engine::ValueEditResult DrawRotation(const char* label, Engine::Quaternion& value) {

		ImGui::PushID(label);
		const auto normalized = value.Normalize();
		float angle = Math::RadToDeg(2.0f * std::acos((std::clamp)(normalized.w, -1.0f, 1.0f)));
		const float length = std::sqrt(normalized.x * normalized.x + normalized.y * normalized.y + normalized.z * normalized.z);
		auto* storage = ImGui::GetStateStorage();
		Engine::Vector3 axis(storage->GetFloat(ImGui::GetID("AxisX"), 1.0f),
			storage->GetFloat(ImGui::GetID("AxisY"), 0.0f), storage->GetFloat(ImGui::GetID("AxisZ"), 0.0f));
		if (length > 0.00001f) axis = Engine::Vector3(normalized.x / length, normalized.y / length, normalized.z / length);
		auto result = Engine::MyGUI::DragVector3((std::string(label) + "・回転軸").c_str(), axis, {.dragSpeed = 0.01f});
		const auto angleEdit = Engine::MyGUI::DragFloat((std::string(label) + "・角度").c_str(), angle);
		result.valueChanged |= angleEdit.valueChanged;
		result.anyItemActive |= angleEdit.anyItemActive;
		result.editFinished |= angleEdit.editFinished;
		if (result.valueChanged) {
			const float axisLength = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
			if (axisLength > 0.00001f) {
				axis = axis / axisLength;
				value = Engine::Quaternion::MakeAxisAngle(axis, Math::DegToRad(angle));
				storage->SetFloat(ImGui::GetID("AxisX"), axis.x);
				storage->SetFloat(ImGui::GetID("AxisY"), axis.y);
				storage->SetFloat(ImGui::GetID("AxisZ"), axis.z);
			}
		}
		ImGui::PopID();
		return result;
	}

	// MyGUIの行内で列挙値を選択する
	template <typename T, size_t N>
	Engine::ValueEditResult Select(const char* label, T& value, const std::array<const char*, N>& names) {

		Engine::ValueEditResult result;
		if (!Engine::MyGUI::BeginPropertyRow(label)) return result;
		ImGui::PushID(label);
		const size_t index = static_cast<size_t>(value);
		if (ImGui::BeginCombo("##Value", index < N ? names[index] : "未設定")) {
			for (size_t i = 0; i < N; ++i) if (ImGui::Selectable(names[i], index == i)) {
				value = static_cast<T>(i); result.valueChanged = true;
			}
			ImGui::EndCombo();
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged;
		ImGui::PopID();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}
}

Engine::ValueEditResult Engine::SimpleAnimationFields::DrawValue(const char* label, SimpleAnimationValue& value) {

	return std::visit([&](auto& typed) -> ValueEditResult {
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, float>) return MyGUI::DragFloat(label, typed, {.dragSpeed = 0.01f});
		else if constexpr (std::is_same_v<T, int32_t>) return MyGUI::DragInt(label, typed);
		else if constexpr (std::is_same_v<T, Vector2>) return MyGUI::DragVector2(label, typed, {.dragSpeed = 0.01f});
		else if constexpr (std::is_same_v<T, Vector3>) return MyGUI::DragVector3(label, typed, {.dragSpeed = 0.01f});
		else if constexpr (std::is_same_v<T, Vector4>) return MyGUI::DragVector4(label, typed, {.dragSpeed = 0.01f});
		else if constexpr (std::is_same_v<T, Quaternion>) return DrawRotation(label, typed);
		else return MyGUI::ColorEdit(label, typed, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	}, value);
}

void Engine::SimpleAnimationFields::DrawCurrentValue(const char* label, const SimpleAnimationValue& value) {

	const std::string text = std::visit([](const auto& typed) -> std::string {
		using T = std::decay_t<decltype(typed)>;
		if constexpr (std::is_same_v<T, int32_t>) return std::format("{}", typed);
		else if constexpr (std::is_same_v<T, float>) return std::format("{:.3f}", typed);
		else if constexpr (std::is_same_v<T, Color3>) return std::format("R {:.3f}  G {:.3f}  B {:.3f}", typed.r, typed.g, typed.b);
		else if constexpr (std::is_same_v<T, Color4>) return std::format("R {:.3f}  G {:.3f}  B {:.3f}  A {:.3f}", typed.r, typed.g, typed.b, typed.a);
		else if constexpr (std::is_same_v<T, Vector2>) return std::format("X {:.3f}  Y {:.3f}", typed.x, typed.y);
		else if constexpr (std::is_same_v<T, Vector3>) return std::format("X {:.3f}  Y {:.3f}  Z {:.3f}", typed.x, typed.y, typed.z);
		else return std::format("X {:.3f}  Y {:.3f}  Z {:.3f}  W {:.3f}", typed.x, typed.y, typed.z, typed.w);
	}, value);
	MyGUI::Text(label, text);
}

Engine::ValueEditResult Engine::SimpleAnimationFields::DrawSettings(SimpleAnimationSettings& settings, bool allowTypeChange) {

	ValueEditResult result;
	auto field = [&](auto&& draw) {
		const auto edit = draw();
		result.valueChanged |= edit.valueChanged;
		result.anyItemActive |= edit.anyItemActive;
		result.editFinished |= edit.editFinished;
	};
	auto number = [&](const char* label, double& value, float minimum = 0.0f) {
		float editable = static_cast<float>(value);
		field([&] { auto edit = MyGUI::DragFloat(label, editable, {.dragSpeed = 0.01f, .minValue = minimum, .maxValue = 1000000.0f});
			if (edit.valueChanged) value = editable; return edit; });
	};
	if (allowTypeChange) {
		size_t type = settings.start.index();
		field([&] { auto edit = Select("値の型", type, std::array{"Float", "Vector2", "Vector3", "Vector4", "Color3", "Color4", "Quaternion", "Int"});
			if (edit.valueChanged) settings = MakeSimpleAnimationSettings(type); return edit; });
	}
	field([&] { return Select("アニメーション", settings.mode, std::array{"A→B補間", "Sin", "Cos"}); });
	if (settings.start.index() == 6) {
		field([&] {
			auto edit = Select("回転編集", settings.rotationMode, std::array{"軸＋角度", "オイラー角"});
			if (edit.valueChanged && settings.rotationMode == SimpleAnimationRotationMode::Euler) {
				settings.startEuler = Quaternion::ToEulerAngles(std::get<Quaternion>(settings.start));
				settings.endEuler = Quaternion::ToEulerAngles(std::get<Quaternion>(settings.end));
				settings.centerEuler = Quaternion::ToEulerAngles(std::get<Quaternion>(settings.center));
			} else if (edit.valueChanged) {
				settings.start = Quaternion::FromEulerDegrees(settings.startEuler);
				settings.end = Quaternion::FromEulerDegrees(settings.endEuler);
				settings.center = Quaternion::FromEulerDegrees(settings.centerEuler);
			}
			return edit;
		});
	}
	auto drawValue = [&](const char* label, SimpleAnimationValue& value, Vector3& euler) {
		if (value.index() != 6 || settings.rotationMode != SimpleAnimationRotationMode::Euler) return DrawValue(label, value);
		auto edit = MyGUI::DragVector3(label, euler);
		if (edit.valueChanged) value = Quaternion::FromEulerDegrees(euler);
		return edit;
	};
	if (settings.mode == SimpleAnimationMode::Interpolate) {
		field([&] { return InspectorDrawerCommon::DrawCheckboxField("現在値から開始", settings.startFromCurrent); });
		ImGui::BeginDisabled(settings.startFromCurrent);
		field([&] { return drawValue("A値", settings.start, settings.startEuler); });
		ImGui::EndDisabled();
		field([&] { return drawValue("B値", settings.end, settings.endEuler); });
		number("再生時間", settings.duration);
	} else {
		field([&] { return drawValue("中心値", settings.center, settings.centerEuler); });
		if (settings.center.index() == 6) field([&] { return MyGUI::DragVector3("角度振幅", settings.angularAmplitude); });
		else field([&] { return DrawValue("振幅", settings.amplitude); });
		number("周期秒", settings.period, 0.001f);
		number("位相角度", settings.phaseDegrees, -1000000.0f);
	}
	field([&] {
		ValueEditResult edit;
		if (!MyGUI::BeginPropertyRow("イージング")) return edit;
		const auto before = settings.easing;
		Easing::SelectEasingType(settings.easing, "##Easing", ImGui::GetContentRegionAvail().x);
		edit.valueChanged = settings.easing != before;
		edit.editFinished = edit.valueChanged;
		edit.anyItemActive = ImGui::IsItemActive();
		MyGUI::EndPropertyRow();
		return edit;
	});
	field([&] { return Select("再生方法", settings.repeat, std::array{"1回", Engine::GetInspectorLabel(Engine::InspectorFieldID::Loop), "往復"}); });
	if (settings.repeat != SimpleAnimationRepeat::Once) {
		int32_t count = static_cast<int32_t>((std::min)(settings.cycleCount, uint32_t(INT32_MAX)));
		field([&] { auto edit = MyGUI::DragInt("回数・0は無制限", count, {.minValue = 0});
			if (edit.valueChanged) settings.cycleCount = static_cast<uint32_t>(count); return edit; });
	}
	number("開始待ち", settings.delay);
	number(Engine::GetInspectorLabel(Engine::InspectorFieldID::PlaybackSpeed), settings.speed);
	if (settings.mode != SimpleAnimationMode::Interpolate) number("切替補間時間", settings.blendDuration);
	field([&] { return InspectorDrawerCommon::DrawCheckboxField(Engine::GetInspectorLabel(Engine::InspectorFieldID::UnscaledTime), settings.unscaledTime); });
	return result;
}
