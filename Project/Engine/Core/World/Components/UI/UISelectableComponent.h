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

	// UI選択表示のフレーム状態
	struct UISelectableRuntimeComponent {

		static constexpr bool kSerializable = false;

		UISelectableState state = UISelectableState::Normal;
		UISelectableState previousState = UISelectableState::Normal;
		float colorTransitionElapsed = 0.0f;
		float scaleTransitionElapsed = 0.0f;
		Color4 baseColor = Color4::White();
		Color4 startColor = Color4::White();
		Color4 currentColor = Color4::White();
		Vector3 baseScale = Vector3::AnyInit(1.0f);
		Vector3 startScale = Vector3::AnyInit(1.0f);
		Vector3 currentScale = Vector3::AnyInit(1.0f);
		AssetID baseTexture{};
		bool hadBaseColor = false;
		bool hadBaseTexture = false;
		bool initialized = false;
		bool submitted = false;
		bool normalThisFrame = false;
		bool selectedThisFrame = false;
		bool submittedThisFrame = false;
		bool disabledThisFrame = false;
	};

	struct UISelectableComponent {

		static constexpr bool kHasECSHooks = true;

		bool interactable = true;

		UITransitionStyle normal{};
		UITransitionStyle selected{ Color4(1.1f, 1.1f, 1.1f, 1.0f), Vector2::AnyInit(1.0f) };
		UITransitionStyle submitted{ Color4(0.8f, 0.8f, 0.8f, 1.0f), Vector2::AnyInit(0.96f) };
		UITransitionStyle disabled{ Color4(0.55f, 0.55f, 0.55f, 0.65f), Vector2::AnyInit(1.0f) };

		// Registryから呼ばれるRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UISelectableComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UISelectableComponent& component, nlohmann::json& out);
	};

	// シーン設定のみを反映
	void ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination);

	void from_json(const nlohmann::json& in, UISelectableComponent& component);
	void to_json(nlohmann::json& out, const UISelectableComponent& component);

} // Engine
