#pragma once

//============================================================================
//	include
//============================================================================
#include "InputTypes.h"

// c++
#include <vector>
#include <chrono>

namespace Engine {

	//============================================================================
	//	InputVibrationPlayer class
	//	振動要求の再生時間とモーター出力を管理する
	//============================================================================
	class InputVibrationPlayer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 振動要求を登録
		uint32_t PlayVibration(const InputVibrationParams& params);
		// 指定の振動を停止
		void StopVibration(uint32_t handle);
		// 全ての振動を停止
		void StopAllVibration();
		// 振動の使用状態を設定
		void SetVibrationEnabled(bool enabled);
		// 接続状態と経過時間から出力を更新
		void UpdateVibration(bool gamepadConnected);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 振動エフェクト
		struct VibrationEffect {

			uint32_t handle = 0;
			float left = 0.0f;     // 0..1
			float right = 0.0f;    // 0..1
			float duration = 0.0f; // seconds
			float attack = 0.0f;   // seconds
			float release = 0.0f;  // seconds
			int priority = 0;
			std::chrono::steady_clock::time_point start;
		};

		//--------- variables ----------------------------------------------------

		bool vibrationEnabled_ = true;
		std::vector<VibrationEffect> vibEffects_{};
		uint32_t nextVibHandle_ = 1;
		uint16_t lastMotorLeft_ = 0;
		uint16_t lastMotorRight_ = 0;

		//--------- functions ----------------------------------------------------

		// モーター出力を反映
		void ApplyVibration(uint16_t left, uint16_t right);
		// 正規化値をモーター出力へ変換
		static uint16_t ToMotorSpeed(float v01);
	};
}
