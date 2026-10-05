#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CurveEditorUtility {

	// 色のチャンネル構成を判定する
	bool IsColorChannelSet(std::span<Engine::CurveChannel> channels) {

		if (channels.size() != 3 && channels.size() != 4) {
			return false;
		}
		if (channels[0].name != "R" || channels[1].name != "G" || channels[2].name != "B") {
			return false;
		}
		return channels.size() == 3 || channels[3].name == "A";
	}

	// 軸と角度の構成を判定する
	bool IsQuaternionCurveSet(std::span<Engine::CurveChannel> channels) {

		return channels.size() == 2 && channels[0].name == "Axis" && channels[1].name == "Angle";
	}

	// キー値を軸番号へ変換する
	Engine::Axis ToAxis(float value) {

		const int32_t axisIndex = (std::clamp)(static_cast<int32_t>(std::round(value)), 0, 2);
		return static_cast<Engine::Axis>(axisIndex);
	}

	// 軸番号をキー値へ変換する
	float ToAxisValue(Engine::Axis axis) {

		return static_cast<float>(Engine::EnumAdapter<Engine::Axis>::GetIndex(axis));
	}

	// 軸の表示色を返す
	ImU32 GetAxisColor(float value) {

		switch (ToAxis(value)) {
		case Engine::Axis::X:
			return IM_COL32(255, 80, 80, 255);
		case Engine::Axis::Y:
			return IM_COL32(80, 255, 100, 255);
		case Engine::Axis::Z:
		default:
			return IM_COL32(90, 145, 255, 255);
		}
	}

	// 任意の軸キー列を借用する
	std::span<Engine::CurveQuaternionAxisKey> ToAxisKeySpan(std::vector<Engine::CurveQuaternionAxisKey>* axisKeys) {

		if (!axisKeys) {
			return {};
		}
		return *axisKeys;
	}

	// 代表軸のキー値を取得する
	float GetPrimaryAxisValue(const Engine::CurveQuaternionAxisKey& axisKey) {

		if (axisKey.axes.empty()) {
			return ToAxisValue(Engine::Axis::X);
		}
		return ToAxisValue(axisKey.axes.front());
	}

	// 軸の表示色を返す
	ImU32 GetAxisColor(const Engine::CurveQuaternionAxisKey& axisKey) {

		if (!axisKey.useCustomAxis) {
			if (axisKey.axes.size() == 1) {
				return GetAxisColor(ToAxisValue(axisKey.axes.front()));
			}
			return IM_COL32(245, 220, 80, 255);
		}

		const Engine::Vector3 axis = Engine::QuaternionAxisKeyUtility::GetAxisDirection(axisKey);
		const uint8_t r = static_cast<uint8_t>((std::clamp)(std::abs(axis.x), 0.0f, 1.0f) * 255.0f);
		const uint8_t g = static_cast<uint8_t>((std::clamp)(std::abs(axis.y), 0.0f, 1.0f) * 255.0f);
		const uint8_t b = static_cast<uint8_t>((std::clamp)(std::abs(axis.z), 0.0f, 1.0f) * 255.0f);
		return IM_COL32(r, g, b, 255);
	}

	// 時刻に対応する軸キーを探す
	uint32_t FindQuaternionAxisIndex(std::span<Engine::CurveChannel> channels, float time) {

		if (channels.empty() || channels[0].keys.empty()) {
			return 0;
		}
		if (time <= channels[0].keys.front().time) {
			return 0;
		}
		if (channels[0].keys.back().time <= time) {
			return static_cast<uint32_t>(channels[0].keys.size() - 1);
		}

		auto nextIt = std::upper_bound(channels[0].keys.begin(), channels[0].keys.end(), time,
			[](float t, const Engine::CurveKey& key) { return t < key.time; });
		return static_cast<uint32_t>((nextIt - 1) - channels[0].keys.begin());
	}

	// 軸キーか既定軸を取得する
	Engine::CurveQuaternionAxisKey GetQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::span<Engine::CurveQuaternionAxisKey> axisKeys, uint32_t keyIndex) {

		if (keyIndex < axisKeys.size()) {
			return axisKeys[keyIndex];
		}
		Engine::CurveQuaternionAxisKey axisKey = Engine::QuaternionAxisKeyUtility::MakeDefault();
		if (!channels.empty() && keyIndex < channels[0].keys.size()) {
			axisKey.axes = {ToAxis(channels[0].keys[keyIndex].value)};
		}
		return axisKey;
	}

	// 指定時刻の軸キーを取得する
	Engine::CurveQuaternionAxisKey EvaluateQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::span<Engine::CurveQuaternionAxisKey> axisKeys, float time) {

		return GetQuaternionAxisKey(channels, axisKeys, FindQuaternionAxisIndex(channels, time));
	}

	// 軸情報を表示チャンネルへ同期する
	void SyncQuaternionAxisChannel(
		std::span<Engine::CurveChannel> channels, std::span<Engine::CurveQuaternionAxisKey> axisKeys, uint32_t keyIndex) {

		if (channels.empty() || keyIndex >= channels[0].keys.size()) {
			return;
		}
		const Engine::CurveQuaternionAxisKey axisKey = GetQuaternionAxisKey(channels, axisKeys, keyIndex);
		channels[0].keys[keyIndex].value = GetPrimaryAxisValue(axisKey);
		channels[0].keys[keyIndex].interpolation = Engine::CurveInterpolationMode::Constant;
	}

	// RGBカーブの構成を判定する
	bool IsColorCurveSet(std::span<Engine::CurveChannel> channels) {

		if (channels.size() != 3 && channels.size() != 4) {

			return false;
		}

		return channels[0].name == "R" && channels[1].name == "G" && channels[2].name == "B";
	}

	// Alphaチャンネルの有無を確認する
	bool HasAlphaChannel(std::span<Engine::CurveChannel> channels) {

		return IsColorCurveSet(channels) && channels.size() == 4 && channels[3].name == "A";
	}

	// 軸と角度のキー選択を検証する
	bool IsQuaternionSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return IsQuaternionCurveSet(channels) && selection.channelIndex < channels.size() &&
			   selection.keyIndex < channels[selection.channelIndex].keys.size();
	}

	// 軸キーの選択を検証する
	bool IsQuaternionAxisSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return IsQuaternionCurveSet(channels) && selection.channelIndex == 0 && selection.keyIndex < channels[0].keys.size();
	}

	// 角度キーの選択を検証する
	bool IsQuaternionAngleSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return IsQuaternionCurveSet(channels) && selection.channelIndex == 1 && selection.keyIndex < channels[1].keys.size();
	}

	// RGB代表キーの選択を検証する
	bool IsRGBSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return IsColorCurveSet(channels) && selection.channelIndex == 0 &&

			   selection.keyIndex < channels[0].keys.size() &&

			   selection.keyIndex < channels[1].keys.size() &&

			   selection.keyIndex < channels[2].keys.size();
	}

	// Alphaキーの選択を検証する
	bool IsAlphaSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return HasAlphaChannel(channels) && selection.channelIndex == 3 && selection.keyIndex < channels[3].keys.size();
	}

	// 指定時刻の色を評価する
	Engine::Color4 EvaluateCurveColorAtTime(std::span<Engine::CurveChannel> channels, float time) {

		const float r = channels[0].Evaluate(time);

		const float g = channels[1].Evaluate(time);

		const float b = channels[2].Evaluate(time);

		const float a = HasAlphaChannel(channels) ? channels[3].Evaluate(time) : 1.0f;

		return Engine::Color4(

			(std::clamp)(r, 0.0f, 1.0f),

			(std::clamp)(g, 0.0f, 1.0f),

			(std::clamp)(b, 0.0f, 1.0f),

			(std::clamp)(a, 0.0f, 1.0f));
	}

	// RGBキーを同じ時刻へ追加する
	uint32_t AddColorRGBKey(std::span<Engine::CurveChannel> channels, float time) {

		const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);

		const uint32_t index = channels[0].AddKey(time, color.r);

		channels[1].AddKey(time, color.g);

		channels[2].AddKey(time, color.b);

		return index;
	}

	// RGBキーをまとめて削除する
	bool RemoveColorRGBKey(std::span<Engine::CurveChannel> channels, uint32_t keyIndex) {

		bool removed = false;

		removed |= channels[0].RemoveKey(keyIndex);

		removed |= channels[1].RemoveKey(keyIndex);

		removed |= channels[2].RemoveKey(keyIndex);

		return removed;
	}

	// RGBキーを時刻順に揃える
	void SortColorRGBKeys(std::span<Engine::CurveChannel> channels) {

		channels[0].SortKeys();

		channels[1].SortKeys();

		channels[2].SortKeys();
	}

	// 評価した軸を新しいキーへ引き継ぐ
	uint32_t AddQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys, float time) {

		const Engine::CurveQuaternionAxisKey axisKey = EvaluateQuaternionAxisKey(channels, ToAxisKeySpan(axisKeys), time);
		const float axis = GetPrimaryAxisValue(axisKey);
		const uint32_t index = channels[0].AddKey(time, axis, Engine::CurveInterpolationMode::Constant);
		if (axisKeys) {
			const uint32_t insertIndex = (std::min)(index, static_cast<uint32_t>(axisKeys->size()));
			axisKeys->insert(axisKeys->begin() + insertIndex, axisKey);
		}
		return index;
	}

	// 評価した角度を新しいキーへ引き継ぐ
	uint32_t AddQuaternionAngleKey(std::span<Engine::CurveChannel> channels, float time) {

		return channels[1].AddKey(time, channels[1].Evaluate(time));
	}

	// 軸キーと補助情報を削除する
	bool RemoveQuaternionAxisKey(
		std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys, uint32_t keyIndex) {

		bool removed = false;
		removed |= channels[0].RemoveKey(keyIndex);
		if (axisKeys && keyIndex < axisKeys->size()) {
			axisKeys->erase(axisKeys->begin() + keyIndex);
		}
		return removed;
	}

	// 選択した軸か角度のキーを削除する
	bool RemoveQuaternionKey(std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys,
		const Engine::CurveKeySelection& selection) {

		if (IsQuaternionAxisSelection(channels, selection)) {
			return RemoveQuaternionAxisKey(channels, axisKeys, selection.keyIndex);
		}
		if (IsQuaternionAngleSelection(channels, selection)) {
			return channels[1].RemoveKey(selection.keyIndex);
		}
		return false;
	}

	// 軸と補助情報の順番を揃える
	void SortQuaternionKeys(std::span<Engine::CurveChannel> channels, std::vector<Engine::CurveQuaternionAxisKey>* axisKeys) {

		if (axisKeys) {
			Engine::QuaternionAxisKeyUtility::SortKeys(channels[0], *axisKeys);
			for (uint32_t i = 0; i < channels[0].keys.size(); ++i) {
				SyncQuaternionAxisChannel(channels, ToAxisKeySpan(axisKeys), i);
			}
		} else {
			channels[0].SortKeys();
		}
		channels[1].SortKeys();
	}
} // Engine::CurveEditorUtility
