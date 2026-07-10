#include "CurveGenerator.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

//============================================================================
//	CurveGenerator internal
//============================================================================
namespace {

	// プロパティ行でイージングを選択する、変更があればtrue
	bool DrawEasingComboProperty(const char* label, EasingType& easingType) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return false;
		}

		const EasingType before = easingType;
		const float width = ImGui::GetContentRegionAvail().x;
		Easing::SelectEasingType(easingType, label, width <= 1.0f ? 1.0f : width);
		Engine::MyGUI::EndPropertyRow();
		return before != easingType;
	}
}

//============================================================================
//	CurveGenerator functions
//============================================================================
bool Engine::DrawCurveGenerator(CurveGeneratorState& state,
	std::span<CurveChannel> channels, std::span<const CurveBakeTarget> targets) {

	if (channels.empty() || targets.empty()) {
		return false;
	}

	// 生成条件を縦に並べる
	MyGUI::EnumCombo("生成タイプ", state.type);

	const float maxTime = 0.0f < state.maxKeyTime ? state.maxKeyTime : 10000.0f;
	MyGUI::DragFloat("開始時間", state.startTime, { .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = maxTime, });
	MyGUI::DragFloat("終了時間", state.endTime, { .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = maxTime, });
	MyGUI::DragFloat("開始値", state.startValue, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f, });
	MyGUI::DragFloat("終了値", state.endValue, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f, });

	if (state.type != CurveGeneratorType::Easing) {

		MyGUI::DragFloat("振幅", state.amplitude, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f, });
		MyGUI::DragFloat("周波数", state.frequency, { .dragSpeed = 0.001f,.minValue = 0.0f,.maxValue = 10000.0f, });
		MyGUI::DragFloat("位相", state.phase, { .dragSpeed = 0.001f,.minValue = -10000.0f,.maxValue = 10000.0f, });
	} else {
		DrawEasingComboProperty("イージング", state.easingType);
	}

	MyGUI::DragInt("キー数", state.sampleCount, { .dragSpeed = 1.0f,.minValue = 2,.maxValue = 1024 });
	state.sampleCount = (std::max)(state.sampleCount, 2);

	// 候補から適用先チャネルを選ぶ、候補が1つだけでも明示表示する
	state.targetIndex = std::clamp(state.targetIndex, 0, static_cast<int32_t>(targets.size()) - 1);
	if (MyGUI::BeginPropertyRow("適用先")) {
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##BakeTarget", targets[static_cast<size_t>(state.targetIndex)].label.c_str())) {
			for (int32_t t = 0; t < static_cast<int32_t>(targets.size()); ++t) {
				const bool isSelected = state.targetIndex == t;
				if (ImGui::Selectable(targets[static_cast<size_t>(t)].label.c_str(), isSelected)) {
					state.targetIndex = t;
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		MyGUI::EndPropertyRow();
	}
	MyGUI::Checkbox("範囲内のキーを置き換える", state.replaceKeys);

	if (!ImGui::Button("生成", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
		return false;
	}

	// 開始/終了の入力順に依存しないよう、生成範囲だけ正規化する
	float startTime = (std::min)(state.startTime, state.endTime);
	float endTime = (std::max)(state.startTime, state.endTime);
	if (0.0f < state.maxKeyTime) {

		startTime = (std::min)(startTime, state.maxKeyTime);
		endTime = (std::min)(endTime, state.maxKeyTime);
	}
	const float timeRange = (std::max)(endTime - startTime, 0.001f);

	auto bakeChannel = [&](CurveChannel& channel) {

		if (state.replaceKeys) {
			// 置き換え時は指定範囲内の既存キーだけを消し、範囲外の手作業キーは残す
			channel.keys.erase(std::remove_if(channel.keys.begin(), channel.keys.end(),
				[&](const CurveKey& key) {
					return startTime <= key.time && key.time <= endTime;
				}), channel.keys.end());
		}

		for (int32_t i = 0; i < state.sampleCount; ++i) {
			const float normalized = static_cast<float>(i) / static_cast<float>(state.sampleCount - 1);
			const float time = startTime + timeRange * normalized;
			float value = state.startValue;
			// 生成結果は通常編集しやすいよう、まずはLinearキーとして追加する
			if (state.type == CurveGeneratorType::Sin || state.type == CurveGeneratorType::Cos) {
				const float angle = normalized * state.frequency * 2.0f * std::numbers::pi_v<float> +state.phase;
				const float wave = state.type == CurveGeneratorType::Sin ? std::sin(angle) : std::cos(angle);
				value = state.startValue + wave * state.amplitude;
			} else {
				const float eased = EasedValue(state.easingType, normalized);
				value = state.startValue + (state.endValue - state.startValue) * eased;
			}
			channel.AddKey(time, value, CurveInterpolationMode::Spline);
		}
		};

	// 選択した候補のチャネルへだけベイクする、RGBのように複数chまとめた候補は各chへ適用する
	for (uint32_t channelIndex : targets[static_cast<size_t>(state.targetIndex)].channelIndices) {
		if (channelIndex < channels.size()) {
			bakeChannel(channels[channelIndex]);
		}
	}
	return true;
}
