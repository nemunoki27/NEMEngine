#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
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
		Highlighted,
		Pressed,
		Selected,
		Disabled,
	};

	enum class UINavigationMode :
		uint8_t {

		None,
		Automatic,
		Explicit,
	};

	struct UITransitionStyle {

		Color4 color = Color4::White();
		Vector2 scale = Vector2::AnyInit(1.0f);
		bool overrideTexture = false;
		AssetID texture{};
	};

	struct UISelectableComponent {

		bool interactable = true;
		UINavigationMode navigationMode = UINavigationMode::Automatic;

		UUID targetLocalFileID{};
		UUID upLocalFileID{};
		UUID downLocalFileID{};
		UUID leftLocalFileID{};
		UUID rightLocalFileID{};

		bool useCustomHitArea = false;
		Vector2 hitAreaOffset{};
		Vector2 hitAreaSize = Vector2::AnyInit(100.0f);

		float transitionDuration = 0.08f;
		UITransitionStyle normal{};
		UITransitionStyle highlighted{ Color4(1.1f, 1.1f, 1.1f, 1.0f), Vector2::AnyInit(1.0f) };
		UITransitionStyle pressed{ Color4(0.8f, 0.8f, 0.8f, 1.0f), Vector2::AnyInit(0.96f) };
		UITransitionStyle selected{ Color4(1.1f, 1.1f, 1.1f, 1.0f), Vector2::AnyInit(1.0f) };
		UITransitionStyle disabled{ Color4(0.55f, 0.55f, 0.55f, 0.65f), Vector2::AnyInit(1.0f) };

		// ランタイム遷移状態
		UISelectableState runtimeState = UISelectableState::Normal;
		UISelectableState runtimePreviousState = UISelectableState::Normal;
		float runtimeTransitionElapsed = 0.0f;
		Color4 runtimeBaseColor = Color4::White();
		Color4 runtimeStartColor = Color4::White();
		Color4 runtimeCurrentColor = Color4::White();
		Vector3 runtimeBaseScale = Vector3::AnyInit(1.0f);
		Vector3 runtimeStartScale = Vector3::AnyInit(1.0f);
		Vector3 runtimeCurrentScale = Vector3::AnyInit(1.0f);
		AssetID runtimeBaseTexture{};
		bool runtimeHadBaseTexture = false;
		UUID runtimeTargetLocalFileID{};
		bool runtimeInitialized = false;
	};

	void from_json(const nlohmann::json& in, UISelectableComponent& component);
	void to_json(nlohmann::json& out, const UISelectableComponent& component);

	ENGINE_REGISTER_COMPONENT(UISelectableComponent, "UISelectable");
} // Engine
