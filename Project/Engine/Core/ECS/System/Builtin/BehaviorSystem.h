#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/World/BehaviorWorld.h>
#include <Engine/Core/ECS/System/Interface/ISystem.h>
#include <Engine/Core/Collision/CollisionTypes.h>

namespace Engine {

	//============================================================================
	//	BehaviorSystem class
	//	スクリプトからビヘイビアの実体化、ライフサイクルを管理するシステム
	//============================================================================
	class BehaviorSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BehaviorSystem() = default;
		~BehaviorSystem() = default;

		// ワールド切り替えで呼ばれる
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		// 各更新フェーズ
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		// OnCollisionEnterを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionEnter(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionStayを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionStay(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionExitを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionExit(ECSWorld& world, SystemContext& context, const CollisionContact& collision);

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "BehaviorSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ECSWorld* activeWorld_ = nullptr;
		BehaviorWorld runtime_;
		static BehaviorSystem* activeSystem_;

		//--------- functions ----------------------------------------------------

		// アクティブなワールドを設定
		void EnsureActiveWorld(ECSWorld& world, SystemContext& context);
		// ワールド内のビヘイビアハンドルをリセット
		void ResetRuntimeState(ECSWorld& world);
		// スクリプトコンポーネントを持つ全てのエンティティに対して、スクリプトのビヘイビアの実体化と初期化を行う
		void Prepare(ECSWorld& world, SystemContext& context, bool doSweep);
		// 衝突イベントを対象Entityのビヘイビアへ渡す
		void DispatchCollision(ECSWorld& world, SystemContext& context, const CollisionContact& collision, int32_t phase);
	};
} // Engine
