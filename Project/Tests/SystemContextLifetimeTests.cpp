#include "SystemContextLifetimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Engine;

namespace {

	// callbackの実行順と途中終了を確認する
	class SchedulerLifetimeProbe final : public ISystem {
	public:
		SchedulerLifetimeProbe(std::array<uint32_t, 6>& calls, std::function<void(uint32_t)> action)
			: calls_(calls), action_(std::move(action)) {}
		void OnWorldEnter(ECSWorld&, SystemContext&) override { Invoke(0); }
		void FixedUpdate(ECSWorld&, SystemContext&) override { Invoke(1); }
		void Update(ECSWorld&, SystemContext&) override { Invoke(2); }
		void LateUpdate(ECSWorld&, SystemContext&) override { Invoke(3); }
		void OnWorldExit(ECSWorld&, SystemContext&) override { Invoke(4); }
		void OnSceneInstancesChanged(ECSWorld&, SystemContext&, SceneChangePhase) override { Invoke(5); }
		const char* GetName() const override { return "SchedulerLifetimeProbe"; }

	private:
		//--------- variables ----------------------------------------------------

		std::array<uint32_t, 6>& calls_;	   // phaseごとの実行数
		std::function<void(uint32_t)> action_; // callbackの途中処理

		//--------- functions ----------------------------------------------------

		// 実行数を記録して途中処理を呼ぶ
		void Invoke(uint32_t phase) {

			++calls_[phase];
			action_(phase);
		}
	};
}

bool NEMTests::CheckSystemContextLifetime() {

	// 不正な固定時間を拒否し以前の設定を保つ
	SystemContext context;
	{
		ECSWorld world;
		SystemScheduler scheduler;
		std::array<uint32_t, 6> calls{};
		scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(calls, [](uint32_t) {}), 0);
		scheduler.SetFixedDeltaTime(0.01f);
		for (float value : {0.0f, -0.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
			bool refused = false;
			try {
				scheduler.SetFixedDeltaTime(value);
			} catch (const std::invalid_argument&) {
				refused = true;
			}
			if (!refused) {
				return false;
			}
		}
		context.world = &world;
		context.deltaTime = 0.01f;
		scheduler.Tick(&world, context);
		scheduler.DetachCurrentWorld(context);
		if (calls[1] != 1) {
			return false;
		}
	}
	// 同じ実行順は追加後の並び替えでも登録順を保つ
	{
		ECSWorld world;
		SystemScheduler scheduler;
		std::array<std::array<uint32_t, 6>, 25> calls{};
		std::vector<uint32_t> order;
		const auto add = [&](uint32_t index) {
			scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(calls[index],
									[&, index](uint32_t phase) {
										if (phase == 2) {
											order.push_back(index);
										}
									}),
				1);
		};
		for (uint32_t index = 0; index < 24; ++index) {
			add(index);
		}
		context.world = &world;
		context.deltaTime = 0.0f;
		scheduler.Tick(&world, context);
		add(24);
		order.clear();
		scheduler.Tick(&world, context);
		scheduler.DetachCurrentWorld(context);
		if (order.size() != calls.size()) {
			return false;
		}
		for (uint32_t index = 0; index < order.size(); ++index) {
			if (order[index] != index) {
				return false;
			}
		}
	}

	// 通知上限へ達してもContextへ最新Sceneを残す
	{
		ECSWorld world;
		AssetDatabase database;
		SceneSystem sceneSystem;
		SceneInstanceManager scenes;
		for (uint32_t index = 0; index < 11; ++index) {
			SceneHeader header;
			header.name = "Scene" + std::to_string(index);
			scenes.CreateScratchScene(header);
		}
		// 通知対象を作成順の先頭から切り替える
		scenes.SetActive(scenes.GetAll().front().instanceID);
		WorldCommandServices services;
		services.assetDatabase = &database;
		services.sceneSystem = &sceneSystem;
		services.sceneInstances = &scenes;
		world.SetCommandServices(services);
		context.world = &world;
		context.deltaTime = 0.0f;
		context.SetActiveSceneHeader(&scenes.GetActive()->header);
		SystemScheduler scheduler;
		std::array<uint32_t, 6> calls{};
		scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(calls,
								[&](uint32_t phase) {
									if (phase == 5) {
										world.GetCommandBuffer().EnqueueUnloadScene(scenes.GetActive()->instanceID);
									}
								}),
			0);
		world.GetCommandBuffer().EnqueueUnloadScene(scenes.GetActive()->instanceID);
		scheduler.Tick(&world, context);
		scheduler.DetachCurrentWorld(context);
		if (calls[5] != 8 || !scenes.GetActive() || !context.GetActiveSceneHeader() ||
			context.GetActiveSceneHeader()->name != scenes.GetActive()->header.name ||
			context.GetActiveSceneHeader()->name != "Scene9") {
			return false;
		}
	}
	SceneHeader source;
	source.guid = AssetID::New();
	source.name = "Original";
	source.subScenes.push_back({Engine::UUID::New(), "Child", AssetID::New(), true});
	context.SetActiveSceneHeader(&source);
	const auto retained = context.GetActiveSceneHeaderSnapshot();
	SystemContext copied = context;
	source.name = "Changed";
	source.subScenes.clear();
	context.SetActiveSceneHeader(&source);
	// 更新とContext破棄から独立したScene情報を保持する
	if (!retained || retained->name != "Original" || retained->subScenes.size() != 1 ||
		copied.GetActiveSceneHeader() != retained.get() || context.GetActiveSceneHeader()->name != "Changed") {
		return false;
	}
	context.SetActiveSceneHeader(nullptr);
	copied.SetActiveSceneHeader(nullptr);
	if (context.GetActiveSceneHeader() || retained->subScenes.front().slotName != "Child") {
		return false;
	}

	for (uint32_t phase = 0; phase < 6; ++phase) {
		for (bool throwAfterEnding : {false, true}) {
			auto world = std::make_unique<ECSWorld>();
			AssetDatabase database;
			SceneSystem sceneSystem;
			SceneInstanceManager scenes;
			const auto first = scenes.CreateScratchScene(source);
			scenes.CreateScratchScene(SceneHeader{});
			WorldCommandServices services;
			services.assetDatabase = &database;
			services.sceneSystem = &sceneSystem;
			services.sceneInstances = &scenes;
			world->SetCommandServices(services);
			context.world = world.get();
			context.SetActiveSceneHeader(&source);
			context.deltaTime = 0.01f;
			SystemScheduler scheduler;
			scheduler.SetFixedDeltaTime(0.01f);
			std::array<uint32_t, 6> calls{}, laterCalls{};
			bool canEnd = true;
			scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(calls,
									[&](uint32_t current) {
										if (canEnd && current == phase) {
											world.reset();
											if (throwAfterEnding) {
												throw std::runtime_error("callback ended fixture");
											}
										}
									}),
				0);
			scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(laterCalls, [](uint32_t) {}), 1);
			bool rejected = false;
			try {
				if (phase == 5) {
					world->GetCommandBuffer().EnqueueUnloadScene(first);
				}
				scheduler.Tick(world.get(), context);
				if (phase == 4) {
					scheduler.DetachCurrentWorld(context);
				}
			} catch (const std::runtime_error& error) {
				rejected = !throwAfterEnding || std::string_view(error.what()) == "callback ended fixture";
			}
			// 終了したWorldへ後続Systemを呼ばない
			if (!rejected || world || context.world || context.GetActiveSceneHeader() || calls[phase] != 1 ||
				laterCalls[phase] != 0) {
				std::cerr << "Scheduler ended-world contract failed: " << phase << ' ' << throwAfterEnding << '\n';
				return false;
			}
			// 終了した接続を残さず新しいWorldへ入り直す
			canEnd = false;
			const auto previousCalls = calls;
			const auto previousLaterCalls = laterCalls;
			world = std::make_unique<ECSWorld>();
			context.world = world.get();
			context.deltaTime = 0.0f;
			scheduler.Tick(world.get(), context);
			scheduler.DetachCurrentWorld(context);
			if (!world || calls[0] != previousCalls[0] + 1 || laterCalls[0] != previousLaterCalls[0] + 1) {
				return false;
			}
		}
	}

	// 同じアドレスでも終了したWorldと新しいWorldを区別する
	{
		std::optional<ECSWorld> owner(std::in_place);
		SystemScheduler reused;
		std::array<uint32_t, 6> calls{};
		reused.AddSystem(std::make_unique<SchedulerLifetimeProbe>(calls, [](uint32_t) {}), 0);
		context.world = &*owner;
		context.deltaTime = 0.0f;
		reused.Tick(&*owner, context);
		const auto ended = owner->GetLifetime();
		ECSWorld* address = &*owner;
		owner.reset();
		owner.emplace();
		context.world = &*owner;
		reused.Tick(&*owner, context);
		if (ended->IsAlive() || address != &*owner || context.world != &*owner || calls[0] != 2) {
			return false;
		}
		reused.DetachCurrentWorld(context);
	}

	ECSWorld world;
	context.world = &world;
	context.deltaTime = 0.0f;
	SystemScheduler scheduler;
	std::array<uint32_t, 6> firstCalls{}, addedCalls{};
	bool added = false, refusedReentry = false;
	scheduler.AddSystem(std::make_unique<SchedulerLifetimeProbe>(firstCalls,
							[&](uint32_t phase) {
								if (phase == 2 && !added) {
									// callback中の追加で実行中の一覧を再確保しない
									added = true;
									scheduler.AddSystem(
										std::make_unique<SchedulerLifetimeProbe>(addedCalls, [](uint32_t) {}), 1);
									try {
										scheduler.Tick(&world, context);
									} catch (const std::logic_error&) {
										refusedReentry = true;
									}
								}
							}),
		0);
	scheduler.Tick(&world, context);
	if (!added || !refusedReentry || addedCalls != std::array<uint32_t, 6>{}) {
		return false;
	}
	scheduler.Tick(&world, context);
	scheduler.DetachCurrentWorld(context);
	return addedCalls[0] == 1 && addedCalls[2] == 1 && addedCalls[3] == 1 && addedCalls[4] == 1;
}
