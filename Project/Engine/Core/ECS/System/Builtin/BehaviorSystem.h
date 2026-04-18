#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/World/BehaviorWorld.h>
#include <Engine/Core/ECS/System/Interface/ISystem.h>

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

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "BehaviorSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ECSWorld* activeWorld_ = nullptr;
		BehaviorWorld runtime_;

		//--------- functions ----------------------------------------------------

		// アクティブなワールドを設定
		void EnsureActiveWorld(ECSWorld& world, SystemContext& context);
		// ワールド内のビヘイビアハンドルをリセット
		void ResetRuntimeState(ECSWorld& world);
		// スクリプトコンポーネントを持つ全てのエンティティに対して、スクリプトのビヘイビアの実体化と初期化を行う
		void Prepare(ECSWorld& world, SystemContext& context, bool doSweep);
	};
} // Engine