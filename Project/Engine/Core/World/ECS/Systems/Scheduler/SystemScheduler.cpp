#include "SystemScheduler.h"

//============================================================================
//	SystemScheduler classMethods
//============================================================================

void Engine::SystemScheduler::AddSystem(std::unique_ptr<ISystem> system, int32_t order) {

	// システムを追加する
	Entry entry{};
	entry.order = order;
	entry.system = std::move(system);
	systems_.emplace_back(std::move(entry));

	// システムの処理順をソートする
	SortIfNeeded();
}

void Engine::SystemScheduler::Tick(ECSWorld* activeWorld, SystemContext& context) {

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

	uint32_t steps = 0;
	// 蓄積した時間が固定更新の時間以上で、サブステップの最大数に達していない限り、固定更新を繰り返す
	while (fixedDeltaTime_ <= accumulator_ && steps < maxSubSteps_) {
		for (const auto& entry : systems_) {

			entry.system->FixedUpdate(*currentWorld_, context);
		}
		// 蓄積した時間から固定更新の時間を引く
		accumulator_ -= fixedDeltaTime_;
		++steps;
	}

	// 更新処理
	for (const auto& entry : systems_) {

		entry.system->Update(*currentWorld_, context);
	}

	// 後更新処理
	for (const auto& entry : systems_) {

		entry.system->LateUpdate(*currentWorld_, context);
	}

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

	// システムの処理順をソートする
	std::sort(systems_.begin(), systems_.end(), [](const Entry& entryA, const Entry& entryB) {
		return entryA.order < entryB.order;
		});
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
