#include "Easing.h"

// imgui
#include <imgui.h>

// Ease In Sine
float EaseInSine(float t) {
	return 1.0f - std::cos((t * std::numbers::pi_v<float>) / 2.0f);
}

// Ease Out Sine
float EaseOutSine(float t) {
	return std::sin((t * std::numbers::pi_v<float>) / 2.0f);
}

// Ease In Out Sine
float EaseInOutSine(float t) {
	return -(std::cos(std::numbers::pi_v<float> *t) - 1.0f) / 2.0f;
}

// Ease In Quad
float EaseInQuad(float t) {
	return t * t;
}

// Ease Out Quad
float EaseOutQuad(float t) {
	return 1.0f - (1.0f - t) * (1.0f - t);
}

// Ease In Out Quad
float EaseInOutQuad(float t) {
	return t < 0.5f ? 2.0f * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

// Ease In Cubic
float EaseInCubic(float t) {
	return t * t * t;
}

// Ease Out Cubic
float EaseOutCubic(float t) {
	return 1.0f - std::powf(1.0f - t, 3.0f);
}

// Ease In Out Cubic
float EaseInOutCubic(float t) {
	return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

// Ease In Quart
float EaseInQuart(float t) {
	return t * t * t * t;
}

// Ease Out Quart
float EaseOutQuart(float t) {
	return 1.0f - std::powf(1.0f - t, 4.0f);
}

// Ease In Out Quart
float EaseInOutQuart(float t) {
	return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

// Ease In Quint
float EaseInQuint(float t) {
	return t * t * t * t * t;
}

// Ease Out Quint
float EaseOutQuint(float t) {
	return 1.0f - std::powf(1.0f - t, 5.0f);
}

// Ease In Out Quint
float EaseInOutQuint(float t) {
	return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

// Ease In Expo
float EaseInExpo(float t) {
	return t == 0.0f ? 0.0f : std::powf(2.0f, 10.0f * t - 10.0f);
}

// Ease Out Expo
float EaseOutExpo(float t) {
	return t == 1.0f ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t);
}

// Ease In Out Expo
float EaseInOutExpo(float t) {
	if (t == 0.0f) return 0.0f;
	if (t == 1.0f) return 1.0f;
	return t < 0.5f ? std::powf(2.0f, 20 * t - 10.0f) / 2.0f : (2.0f - std::powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

// Ease In Circ
float EaseInCirc(float t) {
	return 1.0f - std::sqrtf(1.0f - std::powf(t, 2.0f));
}

// Ease Out Circ
float EaseOutCirc(float t) {
	return std::sqrtf(1.0f - std::powf(t - 1.0f, 2.0f));
}

// Ease In Out Circ
float EaseInOutCirc(float t) {
	return t < 0.5f ? (1.0f - std::sqrtf(1.0f - std::powf(2.0f * t, 2.0f))) / 2.0f
		: (std::sqrtf(1.0f - std::powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

// Ease Out Back
float EaseOutBack(float t) {

	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;

	return 1.0f + c3 * std::powf(t - 1.0f, 3) + c1 * std::powf(t - 1.0f, 2);
}

// Ease In Back
float EaseInBack(float t) {

	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;

	return c3 * std::powf(t, 3) - c1 * std::powf(t, 2);
}

float EaseInBounce(float t) {

	return 1.0f - EaseOutBounce(1.0f - t);
}

// Ease Out Bounce
float EaseOutBounce(float t) {

	const float n1 = 7.5625f;
	const float d1 = 2.75f;

	if (t < 1.0f / d1) {
		return n1 * t * t;
	} else if (t < 2.0f / d1) {
		t -= 1.5f / d1;
		return n1 * t * t + 0.75f;
	} else if (t < 2.5f / d1) {
		t -= 2.25f / d1;
		return n1 * t * t + 0.9375f;
	} else {
		t -= 2.625f / d1;
		return n1 * t * t + 0.984375f;
	}
}

float EaseInOutBounce(float t) {

	if (t < 0.5f) {
		return EaseInBounce(t * 2.0f) * 0.5f;
	} else {
		return EaseOutBounce(t * 2.0f - 1.0f) * 0.5f + 0.5f;
	}
}

float EasedValue(EasingType easingType, float t) {

	switch (easingType) {
	case EasingType::Linear: return t;
	case EasingType::EaseInSine: return EaseInSine(t);
	case EasingType::EaseOutSine: return EaseOutSine(t);
	case EasingType::EaseInOutSine: return EaseInOutSine(t);
	case EasingType::EaseInQuad: return EaseInQuad(t);
	case EasingType::EaseOutQuad: return EaseOutQuad(t);
	case EasingType::EaseInOutQuad: return EaseInOutQuad(t);
	case EasingType::EaseInCubic: return EaseInCubic(t);
	case EasingType::EaseOutCubic: return EaseOutCubic(t);
	case EasingType::EaseInOutCubic: return EaseInOutCubic(t);
	case EasingType::EaseInQuart: return EaseInQuart(t);
	case EasingType::EaseOutQuart: return EaseOutQuart(t);
	case EasingType::EaseInOutQuart: return EaseInOutQuart(t);
	case EasingType::EaseInQuint: return EaseInQuint(t);
	case EasingType::EaseOutQuint: return EaseOutQuint(t);
	case EasingType::EaseInOutQuint: return EaseInOutQuint(t);
	case EasingType::EaseInExpo: return EaseInExpo(t);
	case EasingType::EaseOutExpo: return EaseOutExpo(t);
	case EasingType::EaseInOutExpo: return EaseInOutExpo(t);
	case EasingType::EaseInCirc: return EaseInCirc(t);
	case EasingType::EaseOutCirc: return EaseOutCirc(t);
	case EasingType::EaseInOutCirc: return EaseInOutCirc(t);
	case EasingType::EaseInBack: return EaseInBack(t);
	case EasingType::EaseOutBack: return EaseOutBack(t);
	case EasingType::EaseInBounce: return EaseInBounce(t);
	case EasingType::EaseOutBounce: return EaseOutBounce(t);
	case EasingType::EaseInOutBounce: return EaseInOutBounce(t);
	default: return t;
	}
}

void Easing::SelectEasingType(EasingType& easingType, const std::string& lebel, float itemWidth) {

	const char* easeInOptions[] = {
		"EaseInSine","EaseInQuad","EaseInCubic","EaseInQuart","EaseInQuint","EaseInExpo","EaseInCirc","EaseInBack","EaseInBounce"
	};
	const char* easeOutOptions[] = {
		"EaseOutSine","EaseOutQuad","EaseOutCubic","EaseOutQuart","EaseOutQuint","EaseOutExpo","EaseOutCirc","EaseOutBack","EaseOutBounce"
	};
	const char* easeInOutOptions[] = {
		"EaseInOutSine","EaseInOutQuad","EaseInOutCubic","EaseInOutQuart","EaseInOutQuint","EaseInOutExpo","EaseInOutCirc","EaseInOutBounce"
	};

	// enumの基準インデックス
	const int baseIn = static_cast<int>(EasingType::EaseInSine);
	const int baseOut = static_cast<int>(EasingType::EaseOutSine);
	const int baseInOut = static_cast<int>(EasingType::EaseInOutSine);

	// プレビュー文字列
	const int easingIndex = static_cast<int>(easingType);

	// デフォルトでLinearにしておいて後更新させる
	const char* previewLabel = "Linear";
	if (easingIndex >= baseIn && easingIndex < baseOut) {

		previewLabel = easeInOptions[easingIndex - baseIn];
	} else if (easingIndex >= baseOut && easingIndex < baseInOut) {

		previewLabel = easeOutOptions[easingIndex - baseOut];
	} else if (easingIndex >= baseInOut) {

		previewLabel = easeInOutOptions[easingIndex - baseInOut];
	}

	if (ImGui::BeginCombo(("EasingType##" + lebel).c_str(), previewLabel)) {

		// Linear ボタン（押した瞬間に index も更新しておくと同フレームの選択ハイライトも合う）
		if (ImGui::Button("Linear", ImVec2(itemWidth, 24.0f))) {
			easingType = EasingType::Linear;
		}

		ImGui::PushItemWidth(itemWidth);

		// EaseIn
		if (ImGui::BeginCombo("EaseIn", "")) {
			for (int i = 0; i < IM_ARRAYSIZE(easeInOptions); ++i) {
				const bool isSelected = (static_cast<int>(easingType) == baseIn + i);
				if (ImGui::Selectable(easeInOptions[i], isSelected)) {
					easingType = static_cast<EasingType>(baseIn + i);
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// EaseOut
		if (ImGui::BeginCombo("EaseOut", "")) {
			for (int i = 0; i < IM_ARRAYSIZE(easeOutOptions); ++i) {
				const bool isSelected = (static_cast<int>(easingType) == baseOut + i);
				if (ImGui::Selectable(easeOutOptions[i], isSelected)) {
					easingType = static_cast<EasingType>(baseOut + i);
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// EaseInOut
		if (ImGui::BeginCombo("EaseInOut", "")) {
			for (int i = 0; i < IM_ARRAYSIZE(easeInOutOptions); ++i) {
				const bool isSelected = (static_cast<int>(easingType) == baseInOut + i);
				if (ImGui::Selectable(easeInOutOptions[i], isSelected)) {
					easingType = static_cast<EasingType>(baseInOut + i);
				}
				if (isSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::PopItemWidth();
		ImGui::EndCombo();
	}
}
