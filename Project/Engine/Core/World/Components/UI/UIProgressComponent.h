#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	UIProgressComponent structures
	//	切り抜き方向と値の補間でプログレスを表示
	//============================================================================
	// プログレスの切り抜き方向
	enum class UIProgressFillDirection : uint8_t {

		LeftToRight,
		RightToLeft,
		TopToBottom,
		BottomToTop,
	};

	// 対象Materialの変更前の値を保持
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

	// チャンク外で所有する表示値と補間状態
	struct UIProgressRuntimeData {

		float displayedValue = 1.0f;
		float delayedValue = 1.0f;
		float displayStart = 1.0f;
		float delayedStart = 1.0f;
		float targetValue = 1.0f;
		float smoothElapsed = 0.0f;
		float delayedElapsed = 0.0f;
		UIProgressTargetRuntime fillTarget{};
		UIProgressTargetRuntime delayedTarget{};
		bool initialized = false;
	};

	struct UIProgressRuntimeStorageTag;
	using UIProgressRuntimeStorage = GenerationalPool<UIProgressRuntimeData, UIProgressRuntimeStorageTag>;
	using UIProgressRuntimeHandle = UIProgressRuntimeStorage::Handle;

	// ECSチャンクには世代付きハンドルだけを保持する
	struct UIProgressRuntimeComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		UIProgressRuntimeHandle handle{};

		static void OnAdded(ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UIProgressRuntimeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UIProgressRuntimeComponent& component, nlohmann::json& out);
	};

	// プログレスの表示範囲と補間設定
	struct UIProgressComponent {

		static constexpr bool kHasECSHooks = true;

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

		// 登録hookによる実行状態の初期化と解放
		static void OnAdded(ECSWorld& world, const Entity& entity, UIProgressComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, UIProgressComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, UIProgressComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UIProgressComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UIProgressComponent& component, nlohmann::json& out);
	};

	// シーン設定のみを反映
	void ApplyUIProgressAuthoring(const UIProgressComponent& source, UIProgressComponent& destination);

	void from_json(const nlohmann::json& in, UIProgressComponent& component);
	void to_json(nlohmann::json& out, const UIProgressComponent& component);
	void ResetUIProgressRuntime(UIProgressRuntimeData& runtime, float value);
	UIProgressRuntimeData* TryGetUIProgressRuntime(ECSWorld& world, const Entity& entity);
	const UIProgressRuntimeData* TryGetUIProgressRuntime(const ECSWorld& world, const Entity& entity);

} // Engine
