#include "SystemScheduler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <chrono>
#include <vector>

namespace {

	constexpr uint32_t kMaxSceneSyncCount = 8;
}

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
	currentWorld_->ResetFrameStatistics();
	if (context.runtimeWorldBaker) {
		context.runtimeWorldBaker->Flush();
	}

	// Fixed
	context.fixedDeltaTime = fixedDeltaTime_;
	// 長時間停止後の固定更新負債を次フレームへ持ち越さないよう、最大サブステップ分に制限する
	if (0.0f < fixedDeltaTime_ && 0 < maxSubSteps_) {

		const float maxAccumulatedTime = fixedDeltaTime_ * static_cast<float>(maxSubSteps_);
		accumulator_ = std::clamp(accumulator_ + (std::max)(context.deltaTime, 0.0f),
			0.0f, maxAccumulatedTime);
	} else {

		accumulator_ = 0.0f;
	}

	// プロファイラ用にシステムごとの処理時間をFixed/Update/LateUpdate合計で計測する
	systemMsScratch_.assign(systems_.size(), 0.0f);
	std::vector<float>& systemMs = systemMsScratch_;
	auto measure = [&systemMs](size_t index, auto&& fn) {

		const auto begin = std::chrono::high_resolution_clock::now();
		fn();
		const std::chrono::duration<float, std::milli> elapsed = std::chrono::high_resolution_clock::now() - begin;
		systemMs[index] += elapsed.count();
		};
	auto flushWorldCommands = [&](SceneChangePhase phase) {

		SceneInstanceManager* sceneInstances =
			currentWorld_->GetCommandServices().sceneInstances;
		uint64_t sceneRevision = sceneInstances ?
			sceneInstances->GetRevision() : 0;

		currentWorld_->FlushWorldCommands();
		// SceneやPrefabの構造変更を次のLifecycle処理より先にBakeする
		if (context.runtimeWorldBaker) {
			context.runtimeWorldBaker->Flush();
		}
		uint32_t syncCount = 0;
		while (sceneInstances &&
			sceneRevision != sceneInstances->GetRevision() &&
			syncCount < kMaxSceneSyncCount) {

			sceneRevision = sceneInstances->GetRevision();
			// Scene切り替えで無効になったHeader参照をLifecycle処理より先に更新する
			const SceneInstance* activeScene = sceneInstances->GetActive();
			context.activeSceneHeader = activeScene ? &activeScene->header : nullptr;
			// 新しいシーンを描画する前にスクリプト初期化と遷移要求を反映する
			for (size_t i = 0; i < systems_.size(); ++i) {

				measure(i, [&] {
					systems_[i].system->OnSceneInstancesChanged(
						*currentWorld_, context, phase);
					});
			}
			// AwakeやStartから積まれた構造変更を同じ安全地点で確定する
			currentWorld_->FlushWorldCommands();
			if (context.runtimeWorldBaker) {
				context.runtimeWorldBaker->Flush();
			}
			++syncCount;
		}
		if (sceneInstances && sceneRevision != sceneInstances->GetRevision()) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"SystemScheduler: Scene Lifecycleの同期回数が上限を超えました");
		}
		};

	uint32_t steps = 0;
	// 蓄積した時間が固定更新の時間以上で、サブステップの最大数に達していない限り、固定更新を繰り返す
	while (fixedDeltaTime_ <= accumulator_ && steps < maxSubSteps_) {
		for (size_t i = 0; i < systems_.size(); ++i) {

			measure(i, [&] { systems_[i].system->FixedUpdate(*currentWorld_, context); });
		}
		// 各サブステップ後に、スクリプト由来の構造変更コマンドを安全地点で適用する
		flushWorldCommands(SceneChangePhase::FixedUpdate);
		// 蓄積した時間から固定更新の時間を引く
		accumulator_ -= fixedDeltaTime_;
		++steps;
	}

	// 更新処理
	for (size_t i = 0; i < systems_.size(); ++i) {

		measure(i, [&] { systems_[i].system->Update(*currentWorld_, context); });
	}
	// Update中に積まれた構造変更コマンドを適用する
	flushWorldCommands(SceneChangePhase::Update);

	// 後更新処理
	for (size_t i = 0; i < systems_.size(); ++i) {

		measure(i, [&] { systems_[i].system->LateUpdate(*currentWorld_, context); });
	}
	// LateUpdate中に積まれた構造変更コマンドを適用する
	flushWorldCommands(SceneChangePhase::LateUpdate);

	// 計測結果を処理順のままプロファイラへ渡す
	systemTimesScratch_.clear();
	systemTimesScratch_.reserve(systems_.size());
	std::vector<FrameProfiler::NamedTime>& systemTimes = systemTimesScratch_;
	for (size_t i = 0; i < systems_.size(); ++i) {

		const char* name = systems_[i].system->GetName();
		systemTimes.push_back({ name ? name : "Unknown", systemMs[i] });
	}
	FrameProfiler::GetInstance().SetEcsSystemTimes(systemTimes);

	// Update/LateUpdate中に予約されたエンティティ破棄をフレーム終端でまとめて反映する
	currentWorld_->FlushPendingDestroyEntities();

	// Chunkメモリと構造変更量をプロファイラへ渡す
	const ECSWorldStatistics statistics = currentWorld_->GetStatistics();
	FrameProfiler& profiler = FrameProfiler::GetInstance();
	profiler.SetArchetypeCount(statistics.archetypeCount);
	profiler.SetECSStatistics(FrameProfiler::ECSStatistics{
		.entityCount = statistics.aliveEntityCount,
		.archetypeCount = statistics.archetypeCount,
		.chunkSlotCount = statistics.chunkSlotCount,
		.allocatedChunkCount = statistics.allocatedChunkCount,
		.allocatedChunkBytes = statistics.allocatedChunkBytes,
		.payloadBytes = statistics.payloadBytes,
		.structuralMigrationCount = statistics.structuralMigrationCount,
		.relocatedComponentCount = statistics.relocatedComponentCount,
		.relocatedComponentBytes = statistics.relocatedComponentBytes,
		});
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
