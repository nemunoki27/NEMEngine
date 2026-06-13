#include "SystemScheduler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <chrono>
#include <vector>

//============================================================================
//	SystemScheduler classMethods
//============================================================================
void Engine::SystemScheduler::AddSystem(std::unique_ptr<ISystem> system, int32_t order) {

	// システムを追加する
	Entry entry{};
	entry.order = order;
	entry.system = std::move(system);
	systems_.emplace_back(std::move(entry));

	// 追加で並び替えが必要になったことを記録し、次のTickで一度だけソートする
	needsSort_ = true;
}

void Engine::SystemScheduler::Tick(ECSWorld* activeWorld, SystemContext& context) {

	// 追加されたシステムを一度だけ並び替える
	SortIfNeeded();

	// ワールドが切り替わったら
	if (currentWorld_ != activeWorld) {
		// 現在のワールドから全てのシステムを切り離す
		if (currentWorld_) {

			// 切り替え前に遅延破棄を確定して、次のワールドへ古い状態を持ち越さない
			currentWorld_->FlushPendingDestroyEntities();
			DetachWorld(*currentWorld_, context);
		}
		// ワールドを切り替える
		currentWorld_ = activeWorld;
		accumulator_ = 0.0f;

		// 新しいワールドに全てのシステムを付ける
		if (currentWorld_) {

			AttachWorld(*currentWorld_, context);
		}
	}
	// ワールドが無ければ処理しない
	if (!currentWorld_) {
		return;
	}

	// Fixed
	context.fixedDeltaTime = fixedDeltaTime_;
	// 固定更新の時間を蓄積する
	accumulator_ += context.deltaTime;

	// プロファイラ用にシステムごとの処理時間をFixed/Update/LateUpdate合計で計測する
	systemMsScratch_.assign(systems_.size(), 0.0f);
	std::vector<float>& systemMs = systemMsScratch_;
	auto measure = [&systemMs](size_t index, auto&& fn) {

		const auto begin = std::chrono::high_resolution_clock::now();
		fn();
		const std::chrono::duration<float, std::milli> elapsed = std::chrono::high_resolution_clock::now() - begin;
		systemMs[index] += elapsed.count();
		};

	uint32_t steps = 0;
	// 蓄積した時間が固定更新の時間以上で、サブステップの最大数に達していない限り、固定更新を繰り返す
	while (fixedDeltaTime_ <= accumulator_ && steps < maxSubSteps_) {
		for (size_t i = 0; i < systems_.size(); ++i) {

			measure(i, [&] { systems_[i].system->FixedUpdate(*currentWorld_, context); });
		}
		// 各サブステップ後に、スクリプト由来の構造変更コマンドを安全地点で適用する
		currentWorld_->FlushWorldCommands();
		// 蓄積した時間から固定更新の時間を引く
		accumulator_ -= fixedDeltaTime_;
		++steps;
	}

	// 更新処理
	for (size_t i = 0; i < systems_.size(); ++i) {

		measure(i, [&] { systems_[i].system->Update(*currentWorld_, context); });
	}
	// Update中に積まれた構造変更コマンドを適用する
	currentWorld_->FlushWorldCommands();

	// 後更新処理
	for (size_t i = 0; i < systems_.size(); ++i) {

		measure(i, [&] { systems_[i].system->LateUpdate(*currentWorld_, context); });
	}
	// LateUpdate中に積まれた構造変更コマンドを適用する
	currentWorld_->FlushWorldCommands();

	// 計測結果を処理順のままプロファイラへ渡す
	systemTimesScratch_.clear();
	systemTimesScratch_.reserve(systems_.size());
	std::vector<FrameProfiler::NamedTime>& systemTimes = systemTimesScratch_;
	for (size_t i = 0; i < systems_.size(); ++i) {

		const char* name = systems_[i].system->GetName();
		systemTimes.push_back({ name ? name : "Unknown", systemMs[i] });
	}
	FrameProfiler::GetInstance().SetEcsSystemTimes(systemTimes);
	// archetype数をプロファイラへ渡す、ForEachの走査数の目安
	FrameProfiler::GetInstance().SetArchetypeCount(currentWorld_->GetArchetypeCount());

	// Update/LateUpdate中に予約されたエンティティ破棄をフレーム終端でまとめて反映する
	currentWorld_->FlushPendingDestroyEntities();
}

void Engine::SystemScheduler::DetachCurrentWorld(SystemContext& context) {

	// ワールドが無ければ処理しない
	if (!currentWorld_) {
		return;
	}

	// 現在のワールドから全てのシステムを切り離す
	currentWorld_->FlushPendingDestroyEntities();
	DetachWorld(*currentWorld_, context);
	currentWorld_ = nullptr;
	accumulator_ = 0.0f;
}

void Engine::SystemScheduler::SortIfNeeded() {

	// 追加が無ければ並び替えしない
	if (!needsSort_) {
		return;
	}

	// システムの処理順をソートする
	std::sort(systems_.begin(), systems_.end(), [](const Entry& entryA, const Entry& entryB) {
		return entryA.order < entryB.order;
		});
	needsSort_ = false;
}

void Engine::SystemScheduler::AttachWorld(ECSWorld& world, SystemContext& context) {

	for (const auto& entry : systems_) {

		entry.system->OnWorldEnter(world, context);
	}
}

void Engine::SystemScheduler::DetachWorld(ECSWorld& world, SystemContext& context) {

	for (const auto& entry : systems_) {

		entry.system->OnWorldExit(world, context);
	}
}
