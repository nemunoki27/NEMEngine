#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	// チャンク外で所有するスキンアニメーションの実行時データ
	struct SkinnedAnimationRuntimeData {

		AssetID mesh{};
		bool initialized = false;

		std::string currentClip{};
		std::string fromClip{};
		std::string toClip{};

		float time = 0.0f;
		float currentDuration = 0.0f;
		float fromTime = 0.0f;
		float blendTime = 0.0f;

		bool inTransition = false;
		bool animationFinished = false;
		int32_t repeatCount = 0;

		Skeleton bindSkeleton{};
		Skeleton skeleton{};
		std::vector<WellForGPU> palette{};
		std::vector<std::string> availableClips{};
	};

	struct SkinnedAnimationRuntimeStorageTag;
	using SkinnedAnimationRuntimeStorage =
		GenerationalPool<SkinnedAnimationRuntimeData, SkinnedAnimationRuntimeStorageTag>;
	using SkinnedAnimationRuntimeHandle =
		SkinnedAnimationRuntimeStorage::Handle;

	// ECSチャンクには世代付きハンドルだけを保持する
	struct SkinnedAnimationRuntimeComponent {

		static constexpr bool kSerializable = false;
		static constexpr bool kHasECSHooks = true;

		SkinnedAnimationRuntimeHandle handle{};

		static void OnAdded(
			ECSWorld& world, const Entity& entity, SkinnedAnimationRuntimeComponent& component);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, SkinnedAnimationRuntimeComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, SkinnedAnimationRuntimeComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, SkinnedAnimationRuntimeComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const SkinnedAnimationRuntimeComponent& component, nlohmann::json& out);
	};

	//============================================================================
	//	SkinnedAnimationComponent struct
	//============================================================================
	struct SkinnedAnimationComponent {

		static constexpr bool kHasECSHooks = true;

		// アニメーションの有効/無効
		bool enabled = true;
		// ループ再生するか
		bool loop = true;
		bool playInEditMode = true;

		// 再生速度
		float playbackSpeed = 1.0f;

		// アニメーションの遷移時間
		float transitionDuration = 0.15f;

		// 再生するアニメーションクリップの名前
		std::string clip = "Default";

		// エディター上でボーンを表示するか
		bool isDisplayBone = false;

		// Registryから呼ばれるRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, SkinnedAnimationComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, SkinnedAnimationComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, SkinnedAnimationComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, SkinnedAnimationComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const SkinnedAnimationComponent& component, nlohmann::json& out);
	};

	// スキンアニメーションのRuntimeデータを返す
	SkinnedAnimationRuntimeData* TryGetSkinnedAnimationRuntime(
		ECSWorld& world, const Entity& entity);
	const SkinnedAnimationRuntimeData* TryGetSkinnedAnimationRuntime(
		const ECSWorld& world, const Entity& entity);

	// json変換
	void from_json(const nlohmann::json& in, SkinnedAnimationComponent& component);
	void to_json(nlohmann::json& out, const SkinnedAnimationComponent& component);

} // Engine
