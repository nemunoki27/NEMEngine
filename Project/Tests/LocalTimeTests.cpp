#include "LocalTimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Time/LocalTime.h>
#include <Engine/Core/World/Components/Transform/SplineComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/World/Components/Time/TimerComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
	void Require(bool value, const char* message) {
		if (!value) throw std::runtime_error(message);
	}
}

bool NEMTests::TestLocalTime() {

	using namespace Engine;
	try {
		static_assert(sizeof(ParticleSystemComponent) <= 64);
		{
			ECSWorld pendingWorld;
			const Entity owner = pendingWorld.CreateEntity();
			const Entity source = pendingWorld.CreateEntity();
			const auto typeID = ComponentTypeRegistry::GetInstance().GetID<TimeScaleComponent>();
			auto& commands = pendingWorld.GetCommandBuffer();
			commands.StageAddComponent(pendingWorld, owner, typeID);
			pendingWorld.TryGetComponentForBinding<TimeScaleComponent>(owner)->timeScale = 0.25f;
			Require(!pendingWorld.HasComponent<TimeScaleComponent>(owner) &&
				ResolveLocalTime(pendingWorld, owner, {}, TimeChannel::Timer, 0.5f, 1).deltaTime == 0.125f,
				"TimeScale追加直後の同callbackで倍率を取得");
			commands.StageAddComponent(pendingWorld, source, typeID);
			pendingWorld.TryGetComponentForBinding<TimeScaleComponent>(source)->timeScale = 2;
			TimeSourceSettings explicitSource;
			explicitSource.mode = TimeSourceMode::Explicit;
			explicitSource.runtimeSourceID = pendingWorld.GetUUID(source);
			Require(ResolveLocalTime(pendingWorld, owner, explicitSource, TimeChannel::Timer, 0.5f, 1).deltaTime == 1,
				"未公開TimeScaleも明示時間源として即時参照");
			pendingWorld.FlushWorldCommands();
			pendingWorld.WithStableStructure([&] {
				pendingWorld.DestroyEntity(source);
				Require(pendingWorld.IsPendingDestroy(source) &&
					!ResolveLocalTime(pendingWorld, owner, explicitSource, TimeChannel::Timer, 0.5f, 1).valid,
					"破棄予約された明示時間源では公開解除前から停止");
			});
			pendingWorld.WithStableStructure([&] {
				pendingWorld.DestroyEntity(owner);
				TimeSourceSettings unscaled;
				unscaled.mode = TimeSourceMode::Unscaled;
				Require(!ResolveLocalTime(pendingWorld, owner, unscaled, TimeChannel::Timer, 0.5f, 1).valid,
					"破棄予約された受信者へ非スケール時間を渡さない");
			});
		}
		{
			ECSWorld pendingWorld;
			const Entity curve = pendingWorld.CreateEntity();
			const auto typeID = ComponentTypeRegistry::GetInstance().GetID<SplineComponent>();
			auto& commands = pendingWorld.GetCommandBuffer();
			commands.StageAddComponent(pendingWorld, curve, typeID);
			const auto* initial = TryGetSpline(pendingWorld, curve);
			Require(initial && initial->points.size() == 2 && !pendingWorld.HasComponent<SplineComponent>(curve),
				"Spline追加直後に未公開の初期値を取得");
			auto settings = *initial;
			settings.is2D = true;
			settings.drawDebug = false;
			settings.points[0].position = Vector3(2, 3, 9);
			SetSpline(pendingWorld, curve, settings);
			const ECSWorld& readOnly = pendingWorld;
			Require(TryGetSpline(readOnly, curve)->is2D && !TryGetSpline(readOnly, curve)->drawDebug &&
				TryGetSpline(readOnly, curve)->points[0].position.z == 0, "未公開Splineの設定と2D制御点補正");
			const auto handle = pendingWorld.TryGetComponentForBinding<SplineComponent>(curve)->handle;
			auto& storage = pendingWorld.GetStorage().Get<SplineStorage>();
			Require(storage.GetAliveCount() == 1, "Splineの未公開Storageを一度だけ生成");
			pendingWorld.FlushWorldCommands();
			Require(storage.IsAlive(handle) && pendingWorld.GetComponent<SplineComponent>(curve).handle == handle &&
				TryGetSpline(pendingWorld, curve)->points[0].position.y == 3, "Spline公開後も設定と所有を維持");
			pendingWorld.RemoveComponent<SplineComponent>(curve);
			Require(storage.GetAliveCount() == 0, "Spline削除時にStorageを解放");
			commands.StageAddComponent(pendingWorld, curve, typeID);
			SetSpline(pendingWorld, curve, settings);
			const auto cancelled = pendingWorld.TryGetComponentForBinding<SplineComponent>(curve)->handle;
			commands.Clear();
			Require(storage.GetAliveCount() == 0 && !storage.IsAlive(cancelled) && !TryGetSpline(pendingWorld, curve),
				"Spline追加取消時に外部Storageを解放");
			commands.StageAddComponent(pendingWorld, curve, typeID);
			Require(!TryGetSpline(pendingWorld, curve)->is2D, "取消後の追加は新規設定から開始");
			pendingWorld.FlushWorldCommands();
			Require(storage.GetAliveCount() == 1 && pendingWorld.GetComponent<SplineComponent>(curve).handle != cancelled,
				"取消済みSplineのStorageを再公開しない");
		}
		{
			ECSWorld particleWorld;
			const Entity particles = particleWorld.CreateEntity();
			particleWorld.AddComponent<ParticleSystemComponent>(particles);
			TimeSourceSettings clock;
			clock.mode = TimeSourceMode::Unscaled;
			SetParticleSystemTimeSource(particleWorld, particles, clock);
			auto settings = *GetParticleSystemSettings(particleWorld, particles);
			settings.looping = !settings.looping;
			SetParticleSystemSettings(particleWorld, particles, settings);
			Require(GetParticleSystemTimeSource(particleWorld, particles).mode == TimeSourceMode::Unscaled,
				"パーティクル設定の再適用で時間源を維持");
			nlohmann::json data;
			Require(particleWorld.SerializeComponentToJson(particles, "ParticleSystem", data), "時間源をStorageから保存");
			Require(data.at("timeSource").at("mode") == "Unscaled", "パーティクル時間源はSceneに保存される");
			auto cloned = particleWorld.CloneForSerialization();
			const Entity clonedParticles = cloned->FindByUUID(particleWorld.GetUUID(particles));
			Require(GetParticleSystemTimeSource(*cloned, clonedParticles).mode == TimeSourceMode::Unscaled,
				"保存用Worldへ時間源を複製");
			clock.mode = TimeSourceMode::Global;
			SetParticleSystemTimeSource(*cloned, clonedParticles, clock);
			Require(GetParticleSystemTimeSource(particleWorld, particles).mode == TimeSourceMode::Unscaled,
				"複製したWorldの時間源編集を元Worldへ漏らさない");
			ECSWorld restored;
			const Entity restoredParticles = restored.CreateEntity();
			Require(restored.AddComponentFromJson(restoredParticles, "ParticleSystem", data), "Sceneから時間源を復元");
			Require(GetParticleSystemTimeSource(restored, restoredParticles).mode == TimeSourceMode::Unscaled,
				"時間源の保存復元一致");
		}
		ECSWorld world;
		const Entity parent = world.CreateEntity();
		const Entity child = world.CreateEntity();
		const Entity source = world.CreateEntity();
		world.AddComponent<TimeScaleComponent>(parent);
		world.AddComponent<TimeScaleComponent>(child);
		world.AddComponent<TimeScaleComponent>(source);
		world.AddComponent<HierarchyComponent>(child).parent = parent;
		auto& parentTime = world.GetComponent<TimeScaleComponent>(parent);
		auto& childTime = world.GetComponent<TimeScaleComponent>(child);
		auto& sourceTime = world.GetComponent<TimeScaleComponent>(source);
		parentTime.timeScale = 0.5f;
		childTime.timeScale = 0.25f;
		TimeSourceSettings settings;
		auto read = [&](TimeChannel channel = TimeChannel::Timer) {
			return ResolveLocalTime(world, child, settings, channel, 0.5f, 1.0f);
		};
		Require(read().deltaTime == 0.0625f, "Global時間へ親子倍率を一度ずつ適用");
		parentTime.applyToChildren = false;
		Require(read().deltaTime == 0.125f, "子階層へ適用しない親を除外");
		Require(ResolveLocalTime(world, parent, {}, TimeChannel::Timer, 0.5f, 1).deltaTime == 0.25f,
			"子階層OFFでも自身には適用");
		parentTime.applyToChildren = true;
		childTime.inheritParentScale = false;
		Require(read().deltaTime == 0.125f, "継承OFFは親倍率を遮断");
		childTime.inheritParentScale = true;
		childTime.channels = static_cast<uint32_t>(TimeChannel::Particle);
		Require(read().deltaTime == 0.25f && read(TimeChannel::Particle).deltaTime == 0.0625f,
			"チャンネルごとに倍率を選別");
		parentTime.enabled = false;
		Require(read().deltaTime == 0.5f, "無効な時間倍率を無視");
		settings.mode = TimeSourceMode::Global;
		Require(read().deltaTime == 0.5f, "Globalのみは階層を無視");
		settings.mode = TimeSourceMode::Unscaled;
		Require(read().deltaTime == 1.0f, "非スケールはGlobalと階層を無視");
		settings.mode = TimeSourceMode::Explicit;
		Require(!read().valid && read().deltaTime == 0, "未設定の明示参照では停止");
		settings.runtimeSourceID = world.GetUUID(source);
		sourceTime.timeScale = 2;
		Require(read().valid && read().deltaTime == 1, "階層外の動的な時間源を使用");
		sourceTime.timeScale = 0;
		Require(read().valid && read().deltaTime == 0, "時間倍率0で停止");
		sourceTime.timeScale = -1;
		Require(read().deltaTime == 0, "負倍率を安全に停止");
		sourceTime.timeScale = std::numeric_limits<float>::quiet_NaN();
		Require(read().deltaTime == 0.5f, "不正倍率を等速へ補正");
		SimpleAnimationComponent simple;
		simple.timeSource = settings;
		auto simpleSnapshot = nlohmann::json(simple);
		simpleSnapshot["settings"]["duration"] = 2.0f;
		from_json(simpleSnapshot, simple);
		Require(simple.timeSource.runtimeSourceID == settings.runtimeSourceID, "SimpleAnimationの値変更で動的時間源を維持");
		AnimationClipPlayerComponent clip;
		clip.timeSource = settings;
		auto clipSnapshot = nlohmann::json(clip);
		clipSnapshot["speed"] = 2;
		from_json(clipSnapshot, clip);
		Require(clip.timeSource.runtimeSourceID == settings.runtimeSourceID, "AnimationClipの値変更で動的時間源を維持");
		TimerComponent timer;
		timer.timeSource = settings;
		auto timerSnapshot = nlohmann::json(timer);
		timerSnapshot["targetSeconds"] = 2;
		from_json(timerSnapshot, timer);
		Require(timer.timeSource.runtimeSourceID == settings.runtimeSourceID, "Timerの値変更で動的時間源を維持");
		simpleSnapshot["timeSource"]["mode"] = "Global";
		from_json(simpleSnapshot, simple);
		Require(!simple.timeSource.runtimeSourceID, "時間源を切り替えたら旧動的参照を解除");
		const nlohmann::json saved = settings;
		Require(!saved.contains("runtimeSourceID") && saved.get<TimeSourceSettings>().runtimeSourceID == Engine::UUID{},
			"実行時参照を保存データに残さない");
		world.RemoveComponent<TimeScaleComponent>(source);
		Require(!read().valid && read().deltaTime == 0, "時間源の削除後はGlobalへフォールバックしない");
		settings = {};
		world.GetComponent<HierarchyComponent>(child).parent = child;
		Require(!read().valid, "不正な親子循環で無限走査しない");
		return true;
	} catch (const std::exception& error) {
		std::cerr << "LocalTime: " << error.what() << '\n';
		return false;
	}
}
