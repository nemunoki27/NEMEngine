#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Animation/Simple/SimpleAnimationPlayback.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine {

	//============================================================================
	//	UISelectableComponent structures
	//	ボタンなどの選択状態と遷移表示を管理する
	//============================================================================
	// UIの選択と決定の状態
	enum class UISelectableState : uint8_t {

		Normal,
		Selected,
		Submitted,
		Disabled,
	};

	SimpleAnimationSettings MakeUIAnimationSettings(const SimpleAnimationValue& target);

	// 状態に応じた色と形状の表示設定
	struct UITransitionStyle {

		bool animationEnabled = true;
		SimpleAnimationSettings colorAnimation = MakeUIAnimationSettings(Color4::White());
		SimpleAnimationSettings scaleAnimation = MakeUIAnimationSettings(Vector2(1, 1));
		bool overrideTexture = false;
		AssetID texture{};
		AssetID sound{};
		float soundVolume = 1.0f;
	};

	// 選択表示の実行状態
	struct UISelectableRuntimeComponent {

		static constexpr bool kSerializable = false;

		UISelectableState state = UISelectableState::Normal;
		UISelectableState previousState = UISelectableState::Normal;
		SimpleAnimationPlayback colorPlayback{};
		SimpleAnimationPlayback scalePlayback{};
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

	// 選択状態ごとの表示設定
	struct UISelectableComponent {

		static constexpr bool kHasECSHooks = true;

		bool interactable = true;

		UITransitionStyle normal{};
		UITransitionStyle selected{
			.colorAnimation = MakeUIAnimationSettings(Color4(1.1f, 1.1f, 1.1f, 1.0f))
		};
		UITransitionStyle submitted{
			.colorAnimation = MakeUIAnimationSettings(Color4(0.8f, 0.8f, 0.8f, 1.0f)),
			.scaleAnimation = MakeUIAnimationSettings(Vector2::AnyInit(0.96f))
		};
		UITransitionStyle disabled{
			.colorAnimation = MakeUIAnimationSettings(Color4(0.55f, 0.55f, 0.55f, 0.65f))
		};

		// 登録時に呼ばれる実行状態の追加と解放
		static void OnAdded(ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, UISelectableComponent& component);
		// 設定をJSONから読み込む
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UISelectableComponent& component);
		// 設定をJSONへ保存する
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UISelectableComponent& component, nlohmann::json& out);
	};

	// シーン設定のみを反映
	void ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination);

	void from_json(const nlohmann::json& in, UISelectableComponent& component);
	void to_json(nlohmann::json& out, const UISelectableComponent& component);

} // Engine
