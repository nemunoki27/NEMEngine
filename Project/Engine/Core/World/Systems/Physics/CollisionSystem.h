#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionContactHistory.h"
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	struct CollisionComponent;
	struct CollisionRuntimeStateComponent;
	struct TransformComponent;

	//============================================================================
	//	CollisionSystem class
	//	衝突判定、押し戻し、OnCollisionコールバックを管理するシステム
	//============================================================================
	class CollisionSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CollisionSystem() = default;
		~CollisionSystem() override = default;

		// World終了時に接触履歴を破棄する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		// 固定ステップで衝突判定、押し戻し、OnCollisionコールバックを実行する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		// Edit中の衝突表示を更新する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システム名を取得する
		const char* GetName() const override { return "CollisionSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		CollisionContactHistory history_;

		//--------- functions ----------------------------------------------------

		// 衝突判定を実行し、必要なら押し戻しとコールバックを処理する
		void UpdateCollisions(ECSWorld& world, SystemContext& context, bool applyResponse);
	};
}
