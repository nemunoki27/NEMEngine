#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

namespace Engine {

	//============================================================================
	//	SystemScheduler class
	//	システムの処理順を管理するクラス
	//============================================================================
	class SystemScheduler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SystemScheduler() = default;
		~SystemScheduler() = default;

		// システムを追加する、orderが小さいほど先に処理される
		void AddSystem(std::unique_ptr<ISystem> system, int32_t order);

		// フレーム更新
		void Tick(ECSWorld* activeWorld, SystemContext& context);

		// ワールドを終了させる
		void DetachCurrentWorld(SystemContext& context);

		//--------- accessor -----------------------------------------------------

		// サブステップの最大数の設定
		void SetMaxSubSteps(uint32_t n) { maxSubSteps_ = n; }
		// 固定更新の時間の設定
		void SetFixedDeltaTime(float deltTime) { fixedDeltaTime_ = deltTime; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// システムのエントリー
		struct Entry {

			// 処理順、小さいほど先に処理される
			int32_t order = 0;
			// システム
			std::unique_ptr<ISystem> system;
		};

		//--------- variables ----------------------------------------------------

		// システムエントリーのリスト
		std::vector<Entry> systems_;

		// 現在処理しているワールド
		ECSWorld* currentWorld_ = nullptr;

		// サブステップの最大数
		uint32_t maxSubSteps_ = 32;
		// 固定更新のための時間管理
		float fixedDeltaTime_ = 1.0f / 60.0f;
		// 固定更新のための時間の蓄積
		float accumulator_ = 0.0f;

		//--------- functions ----------------------------------------------------

		// システムの処理順をソートする
		void SortIfNeeded();
		// ワールドを切り替える
		void AttachWorld(ECSWorld& world, SystemContext& context);
		// ワールドを終了させる
		void DetachWorld(ECSWorld& world, SystemContext& context);
	};
} // Engine