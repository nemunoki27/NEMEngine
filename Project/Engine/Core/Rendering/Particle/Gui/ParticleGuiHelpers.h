#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleValue.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleLoopSettings.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleGui
	//	パーティクル編集UIの共通ヘルパー、形状とモジュールとツールで共用する
	//============================================================================
	namespace ParticleGui {

		// float編集の共通設定
		FloatEditSetting MakeDragSetting(float minValue, float maxValue, float dragSpeed = 0.01f);

		// 定数かランダムかを切り替えられるfloat値を編集する、変更があればtrue
		bool DrawParticleValueFloat(const char* label, ParticleValue<float>& value,
			const FloatEditSetting& setting);
		// 定数かランダムかを切り替えられるuint値を編集する、変更があればtrue
		bool DrawParticleValueUInt(const char* label, ParticleValue<uint32_t>& value);

		// イージングを選択する、変更があればtrue
		bool SelectEasing(EasingType& easing);

		// 進行度のループ設定を編集する、変更があればtrue
		bool DrawLoopSettings(ParticleLoopSettings& loop);

		// jsonのfloat値を編集する、変更があればtrue
		bool DragJsonFloat(const char* label, nlohmann::json& params, const char* key,
			float defaultValue, const FloatEditSetting& setting);
	}
} // Engine
