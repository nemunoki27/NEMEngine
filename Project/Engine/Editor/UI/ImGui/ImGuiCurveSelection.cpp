#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CurveEditorUtility {

	// キーの選択状態を取得する
	bool IsKeySelected(const Engine::CurveEditorState& state, uint32_t channelIndex, uint32_t keyIndex) {

		return state.IsSelected(channelIndex, keyIndex);
	}

	// マウスに近いキーを探す
	bool HitTestKey(const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state,
		const ImVec2& mouse, Engine::CurveKeySelection& outSelection) {

		float bestDistanceSq = kCurveKeyRadius * kCurveKeyRadius * 4.0f;
		bool found = false;

		if (IsColorCurveSet(channels)) {
			if (state.IsChannelVisible(0)) {
				const uint32_t rgbKeyCount = (std::min)({static_cast<uint32_t>(channels[0].keys.size()),
					static_cast<uint32_t>(channels[1].keys.size()), static_cast<uint32_t>(channels[2].keys.size())});
				for (uint32_t keyIndex = 0; keyIndex < rgbKeyCount; ++keyIndex) {
					const float time = channels[0].keys[keyIndex].time;
					const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);
					const float previewValue = (color.r + color.g + color.b) / 3.0f;
					const ImVec2 keyPos = WorldToScreen(rect, state, time, previewValue);
					const float dx = keyPos.x - mouse.x;
					const float dy = keyPos.y - mouse.y;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq <= bestDistanceSq) {
						bestDistanceSq = distanceSq;
						outSelection = {0, keyIndex};
						found = true;
					}
				}
			}

			if (HasAlphaChannel(channels) && state.IsChannelVisible(3)) {
				for (uint32_t keyIndex = 0; keyIndex < channels[3].keys.size(); ++keyIndex) {
					const Engine::CurveKey& key = channels[3].keys[keyIndex];
					const ImVec2 keyPos = WorldToScreen(rect, state, key.time, key.value);
					const float dx = keyPos.x - mouse.x;
					const float dy = keyPos.y - mouse.y;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq <= bestDistanceSq) {
						bestDistanceSq = distanceSq;
						outSelection = {3, keyIndex};
						found = true;
					}
				}
			}
			return found;
		}

		if (IsQuaternionCurveSet(channels)) {

			for (uint32_t channelIndex = 0; channelIndex < static_cast<uint32_t>(channels.size()); ++channelIndex) {
				if (!state.IsChannelVisible(channelIndex)) {
					continue;
				}
				for (uint32_t keyIndex = 0; keyIndex < channels[channelIndex].keys.size(); ++keyIndex) {
					const Engine::CurveKey& key = channels[channelIndex].keys[keyIndex];
					const ImVec2 keyPos = WorldToScreen(rect, state, key.time, key.value);
					const float dx = keyPos.x - mouse.x;
					const float dy = keyPos.y - mouse.y;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq <= bestDistanceSq) {
						bestDistanceSq = distanceSq;
						outSelection = {channelIndex, keyIndex};
						found = true;
					}
				}
			}
			return found;
		}

		for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
			if (!state.IsChannelVisible(channelIndex)) {
				continue;
			}

			const Engine::CurveChannel& channel = channels[channelIndex];
			for (uint32_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex) {
				const Engine::CurveKey& key = channel.keys[keyIndex];
				const ImVec2 keyPos = WorldToScreen(rect, state, key.time, key.value);
				const float dx = keyPos.x - mouse.x;
				const float dy = keyPos.y - mouse.y;
				const float distanceSq = dx * dx + dy * dy;
				if (distanceSq <= bestDistanceSq) {
					bestDistanceSq = distanceSq;
					outSelection = {channelIndex, keyIndex};
					found = true;
				}
			}
		}
		return found;
	}

	// 選択キーを後ろから削除する
	void DeleteSelectedKeys(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys) {

		if (IsQuaternionCurveSet(channels)) {
			std::sort(state.selectedKeys.begin(), state.selectedKeys.end(),
				[](const Engine::CurveKeySelection& lhs, const Engine::CurveKeySelection& rhs) {
					return lhs.keyIndex > rhs.keyIndex;
				});
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				if (IsQuaternionSelection(channels, selection)) {
					RemoveQuaternionKey(channels, quaternionAxisKeys, selection);
				}
			}
			state.ClearSelection();
			return;
		}

		// 色カーブはR/G/Bを1キーとしてまとめて消し、Alphaは別チャンネルとして消す
		if (IsColorCurveSet(channels)) {
			std::vector<uint32_t> rgbKeys{};
			std::vector<uint32_t> alphaKeys{};
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				if (HasAlphaChannel(channels) && selection.channelIndex == 3u) {
					alphaKeys.emplace_back(selection.keyIndex);
				} else if (selection.channelIndex < 3u) {
					rgbKeys.emplace_back(selection.keyIndex);
				}
			}
			// indexずれを避けるため降順にし、重複keyは1回だけ消す
			const auto sortUniqueDesc = [](std::vector<uint32_t>& keys) {
				std::sort(keys.begin(), keys.end(), [](uint32_t lhs, uint32_t rhs) { return lhs > rhs; });
				keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
			};
			sortUniqueDesc(rgbKeys);
			sortUniqueDesc(alphaKeys);
			for (uint32_t keyIndex : rgbKeys) {
				RemoveColorRGBKey(channels, keyIndex);
			}
			for (uint32_t keyIndex : alphaKeys) {
				if (3u < channels.size()) {
					channels[3].RemoveKey(keyIndex);
				}
			}
			state.ClearSelection();
			return;
		}

		// 後ろから削除できるようにチャンネル/キーを降順に並べる
		std::sort(state.selectedKeys.begin(), state.selectedKeys.end(),
			[](const Engine::CurveKeySelection& lhs, const Engine::CurveKeySelection& rhs) {
				if (lhs.channelIndex != rhs.channelIndex) {
					return lhs.channelIndex > rhs.channelIndex;
				}
				return lhs.keyIndex > rhs.keyIndex;
			});
		// indexずれを避けながら削除する
		for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
			if (selection.channelIndex < channels.size()) {
				channels[selection.channelIndex].RemoveKey(selection.keyIndex);
			}
		}
		state.ClearSelection();
	}

	// 選択チャンネルのキーを削除する
	void DeleteSelectedChannelKeys(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state,
		std::vector<Engine::CurveQuaternionAxisKey>* quaternionAxisKeys) {

		if (state.selectedKeys.empty()) {
			return;
		}

		if (IsQuaternionCurveSet(channels)) {
			bool deleteAxis = false;
			bool deleteAngle = false;
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				deleteAxis |= IsQuaternionAxisSelection(channels, selection);
				deleteAngle |= IsQuaternionAngleSelection(channels, selection);
			}
			if (deleteAxis) {
				channels[0].keys.clear();
				if (quaternionAxisKeys) {
					quaternionAxisKeys->clear();
				}
			}
			if (deleteAngle) {
				channels[1].keys.clear();
			}
			state.ClearSelection();
			return;
		}

		if (IsColorCurveSet(channels)) {
			bool deleteRGB = false;
			bool deleteAlpha = false;
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				deleteRGB |= selection.channelIndex < 3u;
				deleteAlpha |= HasAlphaChannel(channels) && selection.channelIndex == 3u;
			}
			if (deleteRGB) {
				channels[0].keys.clear();
				channels[1].keys.clear();
				channels[2].keys.clear();
			}
			if (deleteAlpha) {
				channels[3].keys.clear();
			}
			state.ClearSelection();
			return;
		}

		std::vector<uint32_t> channelIndices{};
		channelIndices.reserve(state.selectedKeys.size());
		for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
			if (selection.channelIndex < channels.size()) {
				channelIndices.emplace_back(selection.channelIndex);
			}
		}
		std::sort(channelIndices.begin(), channelIndices.end());
		channelIndices.erase(std::unique(channelIndices.begin(), channelIndices.end()), channelIndices.end());
		for (uint32_t channelIndex : channelIndices) {
			channels[channelIndex].keys.clear();
		}
		state.ClearSelection();
	}
} // Engine::CurveEditorUtility
