#include "InputDeviceConfiguration.h"

//============================================================================
//	include
//============================================================================
#include "InputDeviceState.h"

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

// directInput
#include <dinput.h>

// json
#include <json.hpp>

using namespace Engine;

namespace {

	// 整数の型と範囲を確認する
	int32_t ReadInteger(const nlohmann::json& data, const char* key, int32_t fallback, int32_t minimum, int32_t maximum) {

		const auto value = data.find(key);
		if (value == data.end()) {
			return fallback;
		}
		if (!value->is_number_integer() || value->get<double>() < minimum || value->get<double>() > maximum) {
			throw std::invalid_argument(key);
		}
		return value->get<int32_t>();
	}

	// 非有限値を入力状態へ持ち込まない
	float ReadFloat(const nlohmann::json& data, const char* key, float fallback) {

		const float value = data.value(key, fallback);
		if (!std::isfinite(value)) {
			throw std::invalid_argument(key);
		}
		return value;
	}
}

InputDeviceConfiguration::InputDeviceConfiguration() : mouseReleaseModKey(DIK_LCONTROL), mouseReleaseTriggerKey(DIK_RETURN) {
}

bool InputDeviceConfigurationSerialization::TryRead(const nlohmann::json& data, InputDeviceConfiguration& configuration) {

	if (!data.is_object()) {
		return false;
	}
	try {
		// 読込途中の値は実行中の設定へ反映しない
		InputDeviceConfiguration candidate = configuration;
		candidate.deadZone =
			std::clamp(ReadFloat(data, "deadZone", candidate.deadZone), 0.0f, InputDeviceConfiguration::kMaxStickValue);
		candidate.backgroundInputEnabled = data.value("backgroundInputEnabled", false);
		const auto players = data.find("players");
		if (players != data.end()) {
			if (!players->is_array()) {
				return false;
			}
			for (size_t i = 0; i < players->size() && i < InputDeviceConfiguration::kMaxPlayers; ++i) {
				const auto& player = (*players)[i];
				if (!player.is_object()) {
					return false;
				}
				candidate.playerGamepads[i] =
					ReadInteger(player, "gamepad", static_cast<int32_t>(i), -1, InputDeviceState::kMaxGamepads - 1);
				candidate.playerKeyboardMouse[i] = player.value("keyboardMouse", i == 0);
			}
		}

		// マウス設定も確定前に検証
		candidate.mouseRangeControl = data.value("mouseRangeControl", candidate.mouseRangeControl);
		candidate.mouseAreaPos.x = ReadFloat(data, "mouseAreaPosX", candidate.mouseAreaPos.x);
		candidate.mouseAreaPos.y = ReadFloat(data, "mouseAreaPosY", candidate.mouseAreaPos.y);
		candidate.mouseAreaSize.x = ReadFloat(data, "mouseAreaSizeX", candidate.mouseAreaSize.x);
		candidate.mouseAreaSize.y = ReadFloat(data, "mouseAreaSizeY", candidate.mouseAreaSize.y);
		candidate.mouseReleaseModKey = ReadInteger(data, "mouseReleaseModKey", candidate.mouseReleaseModKey, 0, 255);
		candidate.mouseReleaseTriggerKey =
			ReadInteger(data, "mouseReleaseTriggerKey", candidate.mouseReleaseTriggerKey, 0, 255);
		if (candidate.mouseAreaSize.x < 0.0f || candidate.mouseAreaSize.y < 0.0f) {
			return false;
		}
		configuration = candidate;
		return true;
	} catch (const nlohmann::json::exception&) {
		return false;
	} catch (const std::invalid_argument&) {
		return false;
	}
}

nlohmann::json InputDeviceConfigurationSerialization::Write(const InputDeviceConfiguration& configuration) {

	// 既存の保存キーとPlayer順を維持
	nlohmann::json players = nlohmann::json::array();
	for (uint32_t i = 0; i < InputDeviceConfiguration::kMaxPlayers; ++i) {
		players.push_back(
			{{"gamepad", configuration.playerGamepads[i]}, {"keyboardMouse", configuration.playerKeyboardMouse[i]}});
	}
	return {
		{"deadZone", configuration.deadZone},
		{"backgroundInputEnabled", configuration.backgroundInputEnabled},
		{"players", std::move(players)},
		{"mouseRangeControl", configuration.mouseRangeControl},
		{"mouseAreaPosX", configuration.mouseAreaPos.x},
		{"mouseAreaPosY", configuration.mouseAreaPos.y},
		{"mouseAreaSizeX", configuration.mouseAreaSize.x},
		{"mouseAreaSizeY", configuration.mouseAreaSize.y},
		{"mouseReleaseModKey", configuration.mouseReleaseModKey},
		{"mouseReleaseTriggerKey", configuration.mouseReleaseTriggerKey},
	};
}
