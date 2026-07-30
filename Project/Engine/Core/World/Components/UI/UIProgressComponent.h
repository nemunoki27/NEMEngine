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

	// チャンク外で所有するProgressの実行時データ
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
	using UIProgressRuntimeStorage =
		GenerationalPool<UIProgressRuntimeData, UIProgressRuntimeStorageTag>;
	using UIProgressRuntimeHandle =
		UIProgressRuntimeStorage::Handle;

	// ECSチャンクには世代付きハンドルだけを保持する
	struct UIProgressRuntimeComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		UIProgressRuntimeHandle handle{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, UIProgressRuntimeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, UIProgressRuntimeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const UIProgressRuntimeComponent& component, nlohmann::json& out);
	};

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

		// Registryから呼ばれるRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, UIProgressComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, UIProgressComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, UIProgressComponent& component);
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
	UIProgressRuntimeData* TryGetUIProgressRuntime(
		ECSWorld& world, const Entity& entity);
	const UIProgressRuntimeData* TryGetUIProgressRuntime(
		const ECSWorld& world, const Entity& entity);

} // Engine
