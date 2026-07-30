#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	// カメラシェイクのフレーム状態
	struct CameraShakeRuntimeComponent {

		static constexpr bool kSerializable = false;

		float time = 0.0f;
		Vector3 offset{};
		bool active = false;
	};

	//============================================================================
	//	CameraShakeComponent struct
	//	カメラシェイク
	//============================================================================

	struct CameraShakeComponent {

		static constexpr bool kHasECSHooks = true;

		// 有効/無効フラグ、デフォルトでfalse、trueで開始
		bool enable = false;

		// シェイクの長さ
		float duration = 1.0f;
		// イージング
		EasingType easingType = EasingType::Linear;

		// シェイクの強さ
		Vector3 strength = Vector3::AnyInit(4.0f);

		// Registryから呼ばれるRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, CameraShakeComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, CameraShakeComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, CameraShakeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, CameraShakeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const CameraShakeComponent& component, nlohmann::json& out);
	};

	// json変換
	void from_json(const nlohmann::json& in, CameraShakeComponent& component);
	void to_json(nlohmann::json& out, const CameraShakeComponent& component);

} // Engine
