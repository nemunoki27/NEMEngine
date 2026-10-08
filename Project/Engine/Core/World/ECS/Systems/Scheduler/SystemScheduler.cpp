#include "SystemScheduler.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

// c++
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

	constexpr uint32_t kMaxSceneSyncCount = 8;
}

//============================================================================
//	SystemScheduler classMethods
//============================================================================
void Engine::SystemScheduler::AddSystem(std::unique_ptr<ISystem> system, int32_t order) {

	if (!system) {
		throw std::invalid_argument("追加するSystemがありません");
	}
	// システムを追加する
	Entry entry{};
	entry.order = order;
	entry.system = std::move(system);
	pendingSystems_.emplace_back(std::move(entry));

	// 次の更新開始時に追加順を反映する
	needsSort_ = true;
}

void Engine::SystemScheduler::SetFixedDeltaTime(float deltaTime) {

	// 固定更新が進まない値を設定前に拒否する
	if (!std::isfinite(deltaTime) || deltaTime <= 0.0f) {
		throw std::invalid_argument("固定更新の時間は有限の正数が必要です");
	}
	fixedDeltaTime_ = deltaTime;
}

void Engine::SystemScheduler::Tick(ECSWorld* activeWorld, SystemContext& context) {

	if (running_) {
		throw std::logic_error("SystemSchedulerの更新中に再入できません");
	}
	ScopedValue running(running_, true);
	ScopedCleanup cleanup([this, &context]() noexcept { ReleaseEndedWorld(context); });
	const auto nextLifetime = activeWorld ? activeWorld->GetLifetime() : nullptr;
	// 同じアドレスへ生成された新しいWorldを外さない
	ReleaseEndedWorld(context, activeWorld);
	// 追加されたシステムを一度だけ並び替える
	SortIfNeeded();

	// ワールドが切り替わったら
	if (currentWorld_ != activeWorld) {
		// 現在のワールドから全てのシステムを切り離す
		if (currentWorld_) {

			// World切替前に予約された破棄を確定する
			currentWorld_->FlushPendingDestroyEntities();
			RequireCurrentWorld(context);
			DetachWorld(*currentWorld_, context);
		}
		if (nextLifetime) {
			nextLifetime->ThrowIfEnded();
		}
		// ワールドを切り替える
		currentWorld_ = activeWorld;
		currentWorldLifetime_ = nextLifetime;
		accumulator_ = 0.0f;
	}
	// ワールドが無ければ処理しない
	if (!currentWorld_) {
		return;
	}
	// 追加したSystemも更新前に現在のWorldへ接続する
	AttachWorld(*currentWorld_, context);
	currentWorld_->ResetFrameStatistics();
	if (context.runtimeWorldBaker) {
		context.runtimeWorldBaker->Flush();
		RequireCurrentWorld(context);
	}

	// Fixed
	context.fixedDeltaTime = fixedDeltaTime_;
	// 固定更新の負債を最大サブステップ分に制限する
	if (0.0f < fixedDeltaTime_ && 0 < maxSubSteps_) {

		const float maxAccumulatedTime = fixedDeltaTime_ * static_cast<float>(maxSubSteps_);
		accumulator_ = std::clamp(accumulator_ + (std::max)(context.deltaTime, 0.0f), 0.0f, maxAccumulatedTime);
	} else {

		accumulator_ = 0.0f;
	}

	// 各SystemのFixedとUpdateとLateUpdateを合算する
	const bool profiling = FrameProfiler::GetInstance().IsEnabled();
	if (profiling) {
		systemMsScratch_.assign(systems_.size(), 0.0f);
	}
	std::vector<float>& systemMs = systemMsScratch_;
	auto measure = [this, &context, &systemMs, profiling](size_t index, auto&& fn) {
		// 計測を止めてもシステムの実行順は変えない
		if (!profiling) {
			fn();
			RequireCurrentWorld(context);
			return;
		}
		const auto begin = std::chrono::high_resolution_clock::now();
		fn();
		RequireCurrentWorld(context);
		const std::chrono::duration<float, std::milli> elapsed = std::chrono::high_resolution_clock::now() - begin;
		systemMs[index] += elapsed.count();
	};

	uint32_t steps = 0;
	SceneChangePhase lastPhase = SceneChangePhase::FixedUpdate;
	// 中断後は残りの固定更新を進めない
	while (!IsUpdateInterrupted(context) && fixedDeltaTime_ <= accumulator_ && steps < maxSubSteps_) {
		for (size_t i = 0; i < systems_.size() && !IsUpdateInterrupted(context); ++i) {

			measure(i, [&] { systems_[i].system->FixedUpdate(*currentWorld_, context); });
		}
		// callbackから戻った安全地点で構造変更を確定する
		FlushWorldCommands(context, lastPhase, profiling);
		accumulator_ -= fixedDeltaTime_;
		++steps;
	}

	if (!IsUpdateInterrupted(context)) {

		lastPhase = SceneChangePhase::Update;
		for (size_t i = 0; i < systems_.size() && !IsUpdateInterrupted(context); ++i) {

			measure(i, [&] { systems_[i].system->Update(*currentWorld_, context); });
		}
		FlushWorldCommands(context, lastPhase, profiling);
	}
	if (!IsUpdateInterrupted(context)) {

		lastPhase = SceneChangePhase::LateUpdate;
		for (size_t i = 0; i < systems_.size() && !IsUpdateInterrupted(context); ++i) {

			measure(i, [&] { systems_[i].system->LateUpdate(*currentWorld_, context); });
		}
		FlushWorldCommands(context, lastPhase, profiling);
	}

	// 中断時も予約の確定とBakeを済ませる
	currentWorld_->FlushPendingDestroyEntities();
	RequireCurrentWorld(context);
	if (IsUpdateInterrupted(context)) {

		accumulator_ = 0.0f;
		FlushWorldCommands(context, lastPhase, profiling);
	}
	if (!profiling) {
		return;
	}

	// 計測結果を処理順のままプロファイラへ渡す
	systemTimesScratch_.clear();
	systemTimesScratch_.reserve(systems_.size());
	std::vector<FrameProfiler::NamedTime>& systemTimes = systemTimesScratch_;
	for (size_t i = 0; i < systems_.size(); ++i) {

		const char* name = systems_[i].system->GetName();
		RequireCurrentWorld(context);
		systemTimes.push_back({name ? name : "Unknown", systemMs[i]});
	}
	FrameProfiler::GetInstance().SetECSSystemTimes(systemTimes);

	// Worldの構成と構造変更量をProfilerへ渡す
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

	if (running_) {
		throw std::logic_error("SystemSchedulerの更新中にWorldを切り離せません");
	}
	ScopedValue running(running_, true);
	ScopedCleanup cleanup([this, &context]() noexcept { ReleaseEndedWorld(context); });
	ReleaseEndedWorld(context);
	// ワールドが無ければ処理しない
	if (!currentWorld_) {
		return;
	}

	// 現在のワールドから全てのシステムを切り離す
	currentWorld_->FlushPendingDestroyEntities();
	RequireCurrentWorld(context);
	DetachWorld(*currentWorld_, context);
	currentWorld_ = nullptr;
	currentWorldLifetime_.reset();
	accumulator_ = 0.0f;
}

void Engine::SystemScheduler::SortIfNeeded() {

	// 追加が無ければ並び替えしない
	if (!needsSort_) {
		return;
	}

	// callbackからの追加は次の更新開始時に取り込む
	systems_.reserve(systems_.size() + pendingSystems_.size());
	for (auto& entry : pendingSystems_) {
		systems_.push_back(std::move(entry));
	}
	pendingSystems_.clear();
	// 同じ実行順は登録順を保つ
	std::stable_sort(
		systems_.begin(), systems_.end(), [](const Entry& entryA, const Entry& entryB) { return entryA.order < entryB.order; });
	needsSort_ = false;
}

void Engine::SystemScheduler::AttachWorld(ECSWorld& world, SystemContext& context) {

	for (auto& entry : systems_) {

		if (entry.entered) {
			continue;
		}
		entry.system->OnWorldEnter(world, context);
		RequireCurrentWorld(context);
		entry.entered = true;
	}
}

void Engine::SystemScheduler::DetachWorld(ECSWorld& world, SystemContext& context) {

	for (auto& entry : systems_) {

		if (!entry.entered) {
			continue;
		}
		entry.system->OnWorldExit(world, context);
		RequireCurrentWorld(context);
		entry.entered = false;
	}
}

void Engine::SystemScheduler::FlushWorldCommands(SystemContext& context, SceneChangePhase phase, bool profiling) {

	SceneInstanceManager* sceneInstances = currentWorld_->GetCommandServices().sceneInstances;
	uint64_t sceneRevision = sceneInstances ? sceneInstances->GetRevision() : 0;

	currentWorld_->FlushWorldCommands();
	RequireCurrentWorld(context);
	// Scene変更をLifecycle通知前にBakeする
	if (context.runtimeWorldBaker) {
		context.runtimeWorldBaker->Flush();
		RequireCurrentWorld(context);
	}
	uint32_t syncCount = 0;
	while (sceneInstances && sceneRevision != sceneInstances->GetRevision() && syncCount < kMaxSceneSyncCount) {

		sceneRevision = sceneInstances->GetRevision();
		// Lifecycle通知前にScene情報を更新する
		const SceneInstance* activeScene = sceneInstances->GetActive();
		context.SetActiveSceneHeader(activeScene ? &activeScene->header : nullptr);
		// 描画前に初期化とScene遷移を反映する
		for (size_t i = 0; i < systems_.size(); ++i) {

			if (!profiling) {
				systems_[i].system->OnSceneInstancesChanged(*currentWorld_, context, phase);
				RequireCurrentWorld(context);
				continue;
			}
			const auto begin = std::chrono::high_resolution_clock::now();
			systems_[i].system->OnSceneInstancesChanged(*currentWorld_, context, phase);
			RequireCurrentWorld(context);
			const std::chrono::duration<float, std::milli> elapsed = std::chrono::high_resolution_clock::now() - begin;
			systemMsScratch_[i] += elapsed.count();
		}
		// 初期化からの予約も同じ安全地点で確定する
		currentWorld_->FlushWorldCommands();
		RequireCurrentWorld(context);
		if (context.runtimeWorldBaker) {
			context.runtimeWorldBaker->Flush();
			RequireCurrentWorld(context);
		}
		++syncCount;
	}
	if (sceneInstances && sceneRevision != sceneInstances->GetRevision()) {

		// 通知上限でも最新Sceneの情報を保持する
		const auto* activeScene = sceneInstances->GetActive();
		context.SetActiveSceneHeader(activeScene ? &activeScene->header : nullptr);
		Logger::Output(LogType::Engine, spdlog::level::warn, "SystemScheduler: Scene Lifecycleの同期回数が上限を超えました");
	}
}

void Engine::SystemScheduler::RequireCurrentWorld(SystemContext& context) {

	const auto lifetime = currentWorldLifetime_;
	if (!lifetime->IsAlive()) {
		// 例外を返す前に終了したWorldの借用を外す
		ReleaseEndedWorld(context);
		lifetime->ThrowIfEnded();
	}
}

void Engine::SystemScheduler::ReleaseEndedWorld(SystemContext& context, ECSWorld* activeWorld) noexcept {

	if (!currentWorldLifetime_ || currentWorldLifetime_->IsAlive()) {
		return;
	}
	if (context.world == currentWorld_ && context.world != activeWorld) {
		context.world = nullptr;
		context.SetActiveSceneHeader(nullptr);
	}
	currentWorld_ = nullptr;
	currentWorldLifetime_.reset();
	accumulator_ = 0.0f;
	for (auto& entry : systems_) {
		entry.entered = false;
	}
}

bool Engine::SystemScheduler::IsUpdateInterrupted(SystemContext& context) {

	const bool interrupted = context.IsUpdateInterrupted();
	RequireCurrentWorld(context);
	return interrupted;
}
