#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	UISelectableComponent structures
	//	ボタンなどの選択状態と遷移表示を管理する
	//============================================================================
	enum class UISelectableState :
		uint8_t {

		Normal,
		Selected,
		Submitted,
		Disabled,
	};

	struct UITransitionStyle {

		Color4 color = Color4::White();
		Vector2 scale = Vector2::AnyInit(1.0f);
		float colorTransitionDuration = 0.08f;
		EasingType colorEasing = EasingType::Linear;
		float scaleTransitionDuration = 0.08f;
		EasingType scaleEasing = EasingType::Linear;
		bool overrideTexture = false;
		AssetID texture{};
		bool useAnimationClip = false;
		AssetID animationClip{};
		AssetID sound{};
		float soundVolume = 1.0f;
	};

	struct UISelectableComponent {

		bool interactable = true;

		UITransitionStyle normal{};
		UITransitionStyle selected{ Color4(1.1f, 1.1f, 1.1f, 1.0f), Vector2::AnyInit(1.0f) };
		UITransitionStyle submitted{ Color4(0.8f, 0.8f, 0.8f, 1.0f), Vector2::AnyInit(0.96f) };
		UITransitionStyle disabled{ Color4(0.55f, 0.55f, 0.55f, 0.65f), Vector2::AnyInit(1.0f) };

		// ランタイム遷移状態
		UISelectableState runtimeState = UISelectableState::Normal;
		UISelectableState runtimePreviousState = UISelectableState::Normal;
		float runtimeColorTransitionElapsed = 0.0f;
		float runtimeScaleTransitionElapsed = 0.0f;
		Color4 runtimeBaseColor = Color4::White();
		Color4 runtimeStartColor = Color4::White();
		Color4 runtimeCurrentColor = Color4::White();
		Vector3 runtimeBaseScale = Vector3::AnyInit(1.0f);
		Vector3 runtimeStartScale = Vector3::AnyInit(1.0f);
		Vector3 runtimeCurrentScale = Vector3::AnyInit(1.0f);
		AssetID runtimeBaseTexture{};
		bool runtimeHadBaseColor = false;
		bool runtimeHadBaseTexture = false;
		bool runtimeInitialized = false;
		bool runtimeSubmitted = false;
		bool runtimeNormalThisFrame = false;
		bool runtimeSelectedThisFrame = false;
		bool runtimeSubmittedThisFrame = false;
		bool runtimeDisabledThisFrame = false;
	};

	// シーン設定のみを反映
	void ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination);

	void from_json(const nlohmann::json& in, UISelectableComponent& component);
	void to_json(nlohmann::json& out, const UISelectableComponent& component);

	ENGINE_REGISTER_COMPONENT(UISelectableComponent, "UISelectable");
} // Engine
