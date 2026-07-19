#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	UIRuntimeService structures
	//	Canvas配下のTransformをスクリーン座標へ変換して描画と入力で共有する
	//============================================================================
	struct UIElementRuntime {

		Entity entity = Entity::Null();
		Entity canvas = Entity::Null();
		Matrix4x4 screenMatrix = Matrix4x4::Identity();
		int32_t canvasSortingLayer = 0;
		int32_t canvasOrder = 0;
		uint32_t hierarchyOrder = 0;
	};

	class UIRuntimeService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// Canvas配下のスクリーン行列を再構築する
		void Build(ECSWorld& world, const Vector2& viewportSize);
		// 指定ワールドのランタイムキャッシュを破棄する
		void Clear(ECSWorld& world);
		// スクリーン座標をCanvasローカル座標へ変換する
		bool TryScreenToLocalPoint(const ECSWorld& world, Entity canvas,
			const Vector2& screenPosition, Vector2& outLocalPosition) const;

		//--------- accessor -----------------------------------------------------

		const UIElementRuntime* Find(const ECSWorld& world, Entity entity) const;
		const std::vector<UIElementRuntime>& GetElements(const ECSWorld& world) const;
		Vector2 GetViewportSize(const ECSWorld& world) const;

		void SetGameplayInputBlocked(bool blocked) { gameplayInputBlocked_ = blocked; }
		void SetTransitionInputBlocked(bool blocked) { transitionInputBlocked_ = blocked; }
		bool IsGameplayInputBlocked() const { return gameplayInputBlocked_ || transitionInputBlocked_; }
		bool IsTransitionInputBlocked() const { return transitionInputBlocked_; }

		static UIRuntimeService& GetInstance();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		struct WorldState {

			Vector2 viewportSize{};
			std::vector<UIElementRuntime> elements{};
			std::unordered_map<uint64_t, size_t> lookup{};
		};

		std::unordered_map<const ECSWorld*, WorldState> worlds_{};
		bool gameplayInputBlocked_ = false;
		bool transitionInputBlocked_ = false;
	};
} // Engine
