#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	SystemScheduler class
	//	システムの処理順を管理するクラス
	//============================================================================
	class SystemScheduler {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SystemScheduler() = default;
		~SystemScheduler() = default;

		// 実行順を指定してSystemの追加を予約する
		void AddSystem(std::unique_ptr<ISystem> system, int32_t order);

		// フレーム更新
		void Tick(ECSWorld* activeWorld, SystemContext& context);

		// ワールドを終了させる
		void DetachCurrentWorld(SystemContext& context);

		//--------- accessor -----------------------------------------------------

		// サブステップの最大数の設定
		void SetMaxSubSteps(uint32_t count) { maxSubSteps_ = count; }
		// 固定更新の時間の設定
		void SetFixedDeltaTime(float deltaTime);

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// システムのエントリー
		struct Entry {

			// 処理順、小さいほど先に処理される
			int32_t order = 0;
			// システム
			std::unique_ptr<ISystem> system;
			// 現在のWorldへの接続完了
			bool entered = false;
		};

		//--------- variables ----------------------------------------------------

		// システムエントリーのリスト
		std::vector<Entry> systems_;
		// callbackから追加された次回更新用のSystem
		std::vector<Entry> pendingSystems_;

		// 現在処理しているワールド
		ECSWorld* currentWorld_ = nullptr;
		// Worldを保持せず終了状態を確認する
		std::shared_ptr<const ECSWorldLifetime> currentWorldLifetime_;
		// 同じSchedulerへの再入を拒否する
		bool running_ = false;

		// サブステップの最大数
		uint32_t maxSubSteps_ = 32;
		// 固定更新のための時間管理
		float fixedDeltaTime_ = 1.0f / 60.0f;
		// 固定更新のための時間の蓄積
		float accumulator_ = 0.0f;

		// システム追加後に並び替えが必要かのフラグ
		bool needsSort_ = false;

		// 毎フレーム使い回すシステム計測用の一時バッファ
		std::vector<float> systemMsScratch_;
		std::vector<FrameProfiler::NamedTime> systemTimesScratch_;

		//--------- functions ----------------------------------------------------

		// 構造変更とSceneのLifecycle通知を安全地点で同期する
		void FlushWorldCommands(SystemContext& context, SceneChangePhase phase, bool profiling);
		// システムの処理順をソートする
		void SortIfNeeded();
		// ワールドを切り替える
		void AttachWorld(ECSWorld& world, SystemContext& context);
		// ワールドを終了させる
		void DetachWorld(ECSWorld& world, SystemContext& context);
		// 終了したWorldへcallback後の処理を続けない
		void RequireCurrentWorld(SystemContext& context);
		// 終了したWorldの借用だけを解除する
		void ReleaseEndedWorld(SystemContext& context, ECSWorld* activeWorld = nullptr) noexcept;
		// 中断条件の評価後にもWorldの寿命を確認する
		bool IsUpdateInterrupted(SystemContext& context);
	};
} // Engine
