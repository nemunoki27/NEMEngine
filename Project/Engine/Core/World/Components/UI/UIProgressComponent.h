#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	UIProgressComponent structures
	//	Primitiveを方向指定で切り抜いてプログレス表示する
	//============================================================================
	enum class UIProgressFillDirection :
		uint8_t {

		LeftToRight,
		RightToLeft,
		TopToBottom,
		BottomToTop,
	};

	struct UIProgressTargetRuntime {

		UUID localFileID{};
		MaterialParameterValue progressParameter{};
		MaterialParameterValue directionParameter{};
		MaterialParameterValue primitiveTypeParameter{};
		bool hadProgressParameter = false;
		bool hadDirectionParameter = false;
		bool hadPrimitiveTypeParameter = false;
		bool valid = false;
	};

	struct UIProgressComponent {

		bool enabled = true;
		bool previewInEditMode = false;

		float minValue = 0.0f;
		float maxValue = 1.0f;
		float value = 1.0f;

		UUID delayedTargetLocalFileID{};
		UIProgressFillDirection direction = UIProgressFillDirection::LeftToRight;

		bool smooth = true;
		float smoothDuration = 0.15f;
		EasingType smoothEasing = EasingType::EaseOutSine;

		bool delayed = false;
		AssetID delayedTexture{};
		float delayedWait = 0.2f;
		float delayedDuration = 0.35f;
		EasingType delayedEasing = EasingType::EaseOutSine;
		bool useUnscaledTime = true;

		// ランタイム表示状態
		float runtimeDisplayedValue = 1.0f;
		float runtimeDelayedValue = 1.0f;
		float runtimeDisplayStart = 1.0f;
		float runtimeDelayedStart = 1.0f;
		float runtimeTargetValue = 1.0f;
		float runtimeSmoothElapsed = 0.0f;
		float runtimeDelayedElapsed = 0.0f;
		UIProgressTargetRuntime runtimeFillTarget{};
		UIProgressTargetRuntime runtimeDelayedTarget{};
		bool runtimeInitialized = false;
	};

	// シーン設定のみを反映
	void ApplyUIProgressAuthoring(const UIProgressComponent& source, UIProgressComponent& destination);

	void from_json(const nlohmann::json& in, UIProgressComponent& component);
	void to_json(nlohmann::json& out, const UIProgressComponent& component);
	void ResetUIProgressRuntime(UIProgressComponent& component);

} // Engine
