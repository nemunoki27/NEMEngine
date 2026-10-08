#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CurveEditorUtility {

	// マウスとキー操作をカーブへ反映する
	void HandleGraphInput(const ImRect& graphRect, std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		Engine::CurveEditResult& result, std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys) {

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 mouse = io.MousePos;
		// メニュー上のクリックをグラフへ渡さない
		const bool popupOpen = ImGui::IsPopupOpen("##CurveContextMenu");
		const bool hovered = RectContains(graphRect, mouse) && !popupOpen;

		state.hasHoveredKey = hovered && HitTestKey(graphRect, channels, state, mouse, state.hoveredKey);

		// 削除キーで選択中のキーをまとめて消す
		if (hovered && !state.selectedKeys.empty() &&
			(ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
			DeleteSelectedKeys(channels, state, quaternionAxisKeys);
			result.valueChanged = true;
			result.selectionChanged = true;
			return;
		}

		if (hovered && io.MouseWheel != 0.0f) {
			ZoomTimeAroundMouse(graphRect, state, mouse, io.MouseWheel);
			UpdateHorizontalViewRange(graphRect, state);
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
			state.dragMode = Engine::CurveEditorDragMode::Pan;
			state.dragStartMouse = mouse;
			state.dragLastMouse = mouse;
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (state.hasHoveredKey) {
				if (io.KeyCtrl) {
					state.ToggleSelection(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex);
				} else if (!state.IsSelected(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex)) {
					state.SelectSingle(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex);
				}
				state.dragMode = Engine::CurveEditorDragMode::Key;
				result.selectionChanged = true;
			} else {
				if (!io.KeyCtrl) {
					state.ClearSelection();
					result.selectionChanged = true;
				}
				state.dragMode = Engine::CurveEditorDragMode::Marquee;
				state.marqueeActive = true;
				state.marqueeMin = mouse;
				state.marqueeMax = mouse;
			}
			state.dragStartMouse = mouse;
			state.dragLastMouse = mouse;
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			state.contextMenuWorld = ScreenToWorld(graphRect, state, mouse);
			state.contextMenuOnKey = state.hasHoveredKey;
			if (state.contextMenuOnKey) {
				state.contextMenuKey = state.hoveredKey;
				// 未選択のキーを右クリックしたら選択を切り替える
				if (!state.IsSelected(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex)) {
					state.SelectSingle(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex);
					result.selectionChanged = true;
				}
			}
			ImGui::OpenPopup("##CurveContextMenu");
		}

		if (ImGui::BeginPopup("##CurveContextMenu")) {
			// メニューから選択中のキーを削除する
			if (!state.selectedKeys.empty()) {
				if (ImGui::MenuItem("選択キーを削除")) {
					DeleteSelectedKeys(channels, state, quaternionAxisKeys);
					result.valueChanged = true;
					result.selectionChanged = true;
				}
				if (ImGui::MenuItem("選択しているチャネルキーをすべて削除")) {
					DeleteSelectedChannelKeys(channels, state, quaternionAxisKeys);
					result.valueChanged = true;
					result.selectionChanged = true;
				}
			}
			if (!state.contextMenuOnKey) {
				if (ImGui::BeginMenu("キー追加")) {

					const float time =
						SnapTime(ClampKeyTime(state, state.contextMenuWorld.x), state.snapEnabled, state.snapInterval);

					if (IsQuaternionCurveSet(channels)) {
						if (ImGui::MenuItem("軸")) {
							const uint32_t newKeyIndex = AddQuaternionAxisKey(channels, quaternionAxisKeys, time);
							state.ClearSelection();
							state.selectedKeys.push_back({0, newKeyIndex});
							result.valueChanged = true;
							result.selectionChanged = true;
						}
						if (ImGui::MenuItem("角度")) {
							const uint32_t newKeyIndex = AddQuaternionAngleKey(channels, time);
							state.ClearSelection();
							state.selectedKeys.push_back({1, newKeyIndex});
							result.valueChanged = true;
							result.selectionChanged = true;
						}
					} else if (IsColorCurveSet(channels)) {
						if (ImGui::MenuItem("RGB")) {
							const uint32_t newKeyIndex = AddColorRGBKey(channels, time);
							state.ClearSelection();
							state.selectedKeys.push_back({0, newKeyIndex});
							result.valueChanged = true;
							result.selectionChanged = true;
						}
						if (HasAlphaChannel(channels) && ImGui::MenuItem("Alpha")) {
							const float alpha = (std::clamp)(channels[3].Evaluate(time), 0.0f, 1.0f);
							const uint32_t newKeyIndex = channels[3].AddKey(time, alpha);
							state.ClearSelection();
							state.selectedKeys.push_back({3, newKeyIndex});
							result.valueChanged = true;
							result.selectionChanged = true;
						}
					} else {
						for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
							if (!state.IsChannelVisible(channelIndex)) {
								continue;
							}
							Engine::CurveChannel& channel = channels[channelIndex];
							if (ImGui::MenuItem(channel.name.c_str())) {
								const float value = state.contextMenuWorld.y;
								const uint32_t newKeyIndex = channel.AddKey(time, value);
								state.ClearSelection();
								state.selectedKeys.push_back({channelIndex, newKeyIndex});
								result.valueChanged = true;
								result.selectionChanged = true;
							}
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndPopup();
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Pan && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
			const ImVec2 prevWorld = ScreenToWorld(graphRect, state, state.dragLastMouse);
			const ImVec2 nowWorld = ScreenToWorld(graphRect, state, mouse);
			const float dt = prevWorld.x - nowWorld.x;
			state.visibleTimeMin += dt;
			state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
			UpdateHorizontalViewRange(graphRect, state);
			state.dragLastMouse = mouse;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Key && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			const ImVec2 prevWorld = ScreenToWorld(graphRect, state, state.dragLastMouse);
			const ImVec2 nowWorld = ScreenToWorld(graphRect, state, mouse);
			const float dt = nowWorld.x - prevWorld.x;
			const float dv = nowWorld.y - prevWorld.y;
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				if (IsQuaternionCurveSet(channels) && IsQuaternionAxisSelection(channels, selection)) {
					Engine::CurveKey& axisKey = channels[0].keys[selection.keyIndex];
					const float movedTime =
						SnapTime(ClampKeyTime(state, axisKey.time + dt), state.snapEnabled, state.snapInterval);
					axisKey.time = movedTime;
					SyncQuaternionAxisChannel(channels, ToAxisKeySpan(quaternionAxisKeys), selection.keyIndex);
					axisKey.interpolation = Engine::CurveInterpolationMode::Constant;
					continue;
				}
				if (IsColorCurveSet(channels) && IsRGBSelection(channels, selection)) {
					Engine::CurveKey& keyR = channels[0].keys[selection.keyIndex];
					Engine::CurveKey& keyG = channels[1].keys[selection.keyIndex];
					Engine::CurveKey& keyB = channels[2].keys[selection.keyIndex];
					const float movedTime =
						SnapTime(ClampKeyTime(state, keyR.time + dt), state.snapEnabled, state.snapInterval);
					keyR.time = movedTime;
					keyG.time = movedTime;
					keyB.time = movedTime;
					continue;
				}
				if (selection.channelIndex >= channels.size()) {
					continue;
				}
				Engine::CurveChannel& channel = channels[selection.channelIndex];
				if (selection.keyIndex >= channel.keys.size()) {
					continue;
				}
				Engine::CurveKey& key = channel.keys[selection.keyIndex];
				key.time = SnapTime(ClampKeyTime(state, key.time + dt), state.snapEnabled, state.snapInterval);
				if (IsColorCurveSet(channels) && selection.channelIndex == 3) {
					key.value = (std::clamp)(key.value + dv, 0.0f, 1.0f);
				} else if (IsQuaternionCurveSet(channels) && selection.channelIndex == 0) {
					SyncQuaternionAxisChannel(channels, ToAxisKeySpan(quaternionAxisKeys), selection.keyIndex);
				} else {
					key.value = ClampKeyValueToVisibleRange(state, key.value + dv);
				}
			}
			state.dragLastMouse = mouse;
			result.valueChanged = true;
			result.anyItemActive = true;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Marquee && state.marqueeActive) {
			state.marqueeMax = mouse;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 minPos = state.marqueeMin;
				ImVec2 maxPos = state.marqueeMax;
				NormalizeRect(minPos, maxPos);
				if (!io.KeyCtrl) {
					state.ClearSelection();
				}
				if (IsQuaternionCurveSet(channels)) {
					for (uint32_t channelIndex = 0; channelIndex < static_cast<uint32_t>(channels.size()); ++channelIndex) {
						if (!state.IsChannelVisible(channelIndex)) {
							continue;
						}
						for (uint32_t keyIndex = 0; keyIndex < channels[channelIndex].keys.size(); ++keyIndex) {
							const Engine::CurveKey& key = channels[channelIndex].keys[keyIndex];
							const ImVec2 keyPos = WorldToScreen(graphRect, state, key.time, key.value);
							if (minPos.x <= keyPos.x && keyPos.x <= maxPos.x && minPos.y <= keyPos.y && keyPos.y <= maxPos.y) {
								state.ToggleSelection(channelIndex, keyIndex);
							}
						}
					}
				} else {
					for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
						if (!state.IsChannelVisible(channelIndex)) {
							continue;
						}
						if (IsColorCurveSet(channels) && (channelIndex == 1 || channelIndex == 2)) {
							continue;
						}
						for (uint32_t keyIndex = 0; keyIndex < channels[channelIndex].keys.size(); ++keyIndex) {
							const Engine::CurveKey& key = channels[channelIndex].keys[keyIndex];
							const ImVec2 keyPos = WorldToScreen(graphRect, state, key.time, key.value);
							if (minPos.x <= keyPos.x && keyPos.x <= maxPos.x && minPos.y <= keyPos.y && keyPos.y <= maxPos.y) {
								state.ToggleSelection(
									(IsColorCurveSet(channels) && channelIndex < 3) ? 0u : channelIndex, keyIndex);
							}
						}
					}
				}
				state.marqueeActive = false;
				state.dragMode = Engine::CurveEditorDragMode::None;
				result.selectionChanged = true;
			}
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Key && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (IsQuaternionCurveSet(channels)) {
				SortQuaternionKeys(channels, quaternionAxisKeys);
			} else if (IsColorCurveSet(channels)) {
				SortColorRGBKeys(channels);
				if (HasAlphaChannel(channels)) {
					channels[3].SortKeys();
				}
			} else {
				for (Engine::CurveChannel& channel : channels) {
					channel.SortKeys();
				}
			}
			state.dragMode = Engine::CurveEditorDragMode::None;
			result.editFinished = true;
			result.valueChanged = true;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Pan && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
			state.dragMode = Engine::CurveEditorDragMode::None;
		}
	}
} // Engine::CurveEditorUtility
