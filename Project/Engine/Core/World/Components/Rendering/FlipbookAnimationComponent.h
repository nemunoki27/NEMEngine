#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	FlipbookAnimationComponent struct
	//============================================================================
	// 行ごとの横タイル数
	struct FlipbookTileColumn {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 4;
		static constexpr bool kSerializable = false;

		int32_t value = 1;
	};

	// Systemだけが更新する再生状態
	struct FlipbookAnimationRuntimeComponent {

		static constexpr bool kSerializable = false;

		bool playing = false;
		bool animationFinished = false;
		int32_t repeatCount = 0;
		float elapsed = 0.0f;
	};

	// 連番画像アニメーションの再生
	struct FlipbookAnimationComponent {

		static constexpr bool kHasECSHooks = true;
	
		// アニメーションの有効/無効
		bool enabled = true;
		// ループ再生するか
		bool loop = true;
		// 次のループ再生までの待機時間
		float loopInterval = 0.0f;
		// 編集中でもプレビュー再生するか
		bool playInEditMode = true;
		// 再生終了後、何も表示されないようにするか
		bool endAnimUnDisplay = false;
		// 縦タイル数
		int32_t tilesY = 1;
		// 再生にかかる時間
		float duration = 1.0f;
		// イージング
		EasingType easingType = EasingType::EaseInSine;

		// Registryから呼ばれるタイルBufferとRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, FlipbookAnimationComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, FlipbookAnimationComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, FlipbookAnimationComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, FlipbookAnimationComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const FlipbookAnimationComponent& component, nlohmann::json& out);
	};

	// json適用
	void from_json(const nlohmann::json& in, FlipbookAnimationComponent& component);
	void to_json(nlohmann::json& out, const FlipbookAnimationComponent& component);
	// Entityに付随する行ごとの横タイル数
	std::span<FlipbookTileColumn> GetFlipbookTileColumns(
		ECSWorld& world, const Entity& entity);
	std::span<const FlipbookTileColumn> GetFlipbookTileColumns(
		const ECSWorld& world, const Entity& entity);
	std::span<const int32_t> GetFlipbookTileValues(
		const ECSWorld& world, const Entity& entity);
	void SetFlipbookTileColumns(ECSWorld& world, const Entity& entity,
		std::span<const int32_t> columns);
	// タイル列を含む保存データへ変換する
	void SerializeFlipbookAnimation(
		const FlipbookAnimationComponent& component,
		std::span<const int32_t> columns, nlohmann::json& out);

} // Engine
