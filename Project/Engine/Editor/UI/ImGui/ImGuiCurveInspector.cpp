#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================
#include "ImGuiEnum.h"
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <array>

namespace Engine::CurveEditorUtility {

	// 選択キーの時刻と値を編集する
	void DrawInspector(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		Engine::CurveEditResult& result, float height, std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys) {

		ImGui::BeginChild("##CurveInspector", ImVec2(kCurveInspectorWidth, height), true);
		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		ImGui::TextUnformatted("キー");
		ImGui::Separator();
		if (state.selectedKeys.empty()) {
			ImGui::TextDisabled("キーを選択してください");
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		Engine::CurveKeySelection selection = state.selectedKeys.front();

		if (IsColorCurveSet(channels) && IsRGBSelection(channels, selection)) {
			Engine::CurveKey& keyR = channels[0].keys[selection.keyIndex];
			Engine::CurveKey& keyG = channels[1].keys[selection.keyIndex];
			Engine::CurveKey& keyB = channels[2].keys[selection.keyIndex];
			ImGui::TextUnformatted("RGB");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			float time = keyR.time;
			if (ImGui::DragFloat("時刻", &time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				time = ClampKeyTime(state, time);
				keyR.time = time;
				keyG.time = time;
				keyB.time = time;
				result.valueChanged = true;
			}
			float color[3] = {keyR.value, keyG.value, keyB.value};
			if (ImGui::ColorEdit3("色", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel)) {
				keyR.value = color[0];
				keyG.value = color[1];
				keyB.value = color[2];
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (Engine::ImGuiUtility::EnumCombo<Engine::CurveInterpolationMode>("Interp", &keyR.interpolation)) {
				keyG.interpolation = keyR.interpolation;
				keyB.interpolation = keyR.interpolation;
				result.valueChanged = true;
			}
			if (result.valueChanged) {
				SortColorRGBKeys(channels);
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (IsColorCurveSet(channels) && IsAlphaSelection(channels, selection)) {
			Engine::CurveChannel& channel = channels[3];
			Engine::CurveKey& key = channel.keys[selection.keyIndex];
			ImGui::TextUnformatted("アルファ");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("時刻", &key.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				key.time = ClampKeyTime(state, key.time);
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("値", &key.value, 0.01f, 0.0f, 1.0f, "%.3f")) {
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (Engine::ImGuiUtility::EnumCombo<Engine::CurveInterpolationMode>("Interp", &key.interpolation)) {
				result.valueChanged = true;
			}
			if (result.valueChanged) {
				key.value = (std::clamp)(key.value, 0.0f, 1.0f);
				channel.SortKeys();
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (IsQuaternionAxisSelection(channels, selection)) {
			Engine::CurveKey& axisKey = channels[0].keys[selection.keyIndex];
			Engine::CurveQuaternionAxisKey fallbackAxisKey =
				GetQuaternionAxisKey(channels, ToAxisKeySpan(quaternionAxisKeys), selection.keyIndex);
			Engine::CurveQuaternionAxisKey* axisSetting = &fallbackAxisKey;
			if (quaternionAxisKeys && selection.keyIndex < quaternionAxisKeys->size()) {
				axisSetting = &(*quaternionAxisKeys)[selection.keyIndex];
			}
			ImGui::TextUnformatted("軸");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("時刻", &axisKey.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				axisKey.time = ClampKeyTime(state, axisKey.time);
				result.valueChanged = true;
			}

			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::Checkbox("任意の軸", &axisSetting->useCustomAxis)) {
				result.valueChanged = true;
			}
			if (axisSetting->useCustomAxis) {
				ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
				if (ImGui::DragFloat("軸X", &axisSetting->customAxis.x, 0.01f, -1.0f, 1.0f, "%.3f")) {
					result.valueChanged = true;
				}
				ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
				if (ImGui::DragFloat("軸Y", &axisSetting->customAxis.y, 0.01f, -1.0f, 1.0f, "%.3f")) {
					result.valueChanged = true;
				}
				ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
				if (ImGui::DragFloat("軸Z", &axisSetting->customAxis.z, 0.01f, -1.0f, 1.0f, "%.3f")) {
					result.valueChanged = true;
				}
			} else {
				if (axisSetting->axes.empty()) {
					axisSetting->axes.emplace_back(Engine::Axis::X);
				}
				for (uint32_t axisIndex = 0; axisIndex < axisSetting->axes.size(); ++axisIndex) {
					ImGui::PushID(static_cast<int>(axisIndex));
					ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
					if (Engine::ImGuiUtility::EnumCombo<Engine::Axis>("軸", &axisSetting->axes[axisIndex])) {
						result.valueChanged = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("削除")) {
						axisSetting->axes.erase(axisSetting->axes.begin() + axisIndex);
						if (axisSetting->axes.empty()) {
							axisSetting->axes.emplace_back(Engine::Axis::X);
						}
						result.valueChanged = true;
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
				if (ImGui::Button("軸を追加")) {
					axisSetting->axes.emplace_back(Engine::Axis::X);
					result.valueChanged = true;
				}
			}

			if (result.valueChanged) {
				axisKey.value = GetPrimaryAxisValue(*axisSetting);
				axisKey.interpolation = Engine::CurveInterpolationMode::Constant;
				SortQuaternionKeys(channels, quaternionAxisKeys);
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (IsQuaternionAngleSelection(channels, selection)) {
			Engine::CurveChannel& channel = channels[1];
			Engine::CurveKey& key = channel.keys[selection.keyIndex];
			ImGui::TextUnformatted("角度");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("時刻", &key.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				key.time = ClampKeyTime(state, key.time);
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("角度", &key.value, 0.1f, -36000.0f, 36000.0f, "%.3f")) {
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (Engine::ImGuiUtility::EnumCombo<Engine::CurveInterpolationMode>("Interp", &key.interpolation)) {
				result.valueChanged = true;
			}
			if (result.valueChanged) {
				channel.SortKeys();
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (channels.size() <= selection.channelIndex || channels[selection.channelIndex].keys.size() <= selection.keyIndex) {
			ImGui::TextDisabled("選択が無効です");
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		Engine::CurveChannel& channel = channels[selection.channelIndex];
		Engine::CurveKey& key = channel.keys[selection.keyIndex];
		ImGui::TextUnformatted(ImGuiUtility::GetDisplayName(channel.name.c_str()));
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (ImGui::DragFloat("時刻", &key.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
			key.time = ClampKeyTime(state, key.time);
			result.valueChanged = true;
		}
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (ImGui::DragFloat("値", &key.value, 0.01f, state.visibleValueMin, state.visibleValueMax, "%.3f")) {
			result.valueChanged = true;
		}
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (Engine::ImGuiUtility::EnumCombo<Engine::CurveInterpolationMode>("Interp", &key.interpolation)) {
			result.valueChanged = true;
		}
		if (result.valueChanged) {
			key.value = ClampKeyValueToVisibleRange(state, key.value);
			channel.SortKeys();
			result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
		}
		ImGui::SetWindowFontScale(previousFontScale);
		ImGui::EndChild();
	}
} // Engine::CurveEditorUtility
