#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <array>
#include <cstdint>

// json
#include <json_fwd.hpp>

namespace Engine {

	// 入力デバイスの保存設定
	struct InputDeviceConfiguration {

		static constexpr uint32_t kMaxPlayers = 4;
		static constexpr float kMaxStickValue = 32767.0f;

		InputDeviceConfiguration();

		// 入力判定とPlayer割当
		float deadZone = 8000.0f;
		std::array<int32_t, kMaxPlayers> playerGamepads{0, 1, 2, 3};
		std::array<bool, kMaxPlayers> playerKeyboardMouse{true, false, false, false};
		bool backgroundInputEnabled = false;

		// マウスの移動範囲と解除キー
		bool mouseRangeControl = false;
		Vector2 mouseAreaPos{};
		Vector2 mouseAreaSize{};
		int32_t mouseReleaseModKey = 0;
		int32_t mouseReleaseTriggerKey = 0;
	};

	namespace InputDeviceConfigurationSerialization {

		// 全項目の検証後に設定を置き換える
		bool TryRead(const nlohmann::json& data, InputDeviceConfiguration& configuration);
		// 保存用のJSONを作成する
		nlohmann::json Write(const InputDeviceConfiguration& configuration);
	}
}
