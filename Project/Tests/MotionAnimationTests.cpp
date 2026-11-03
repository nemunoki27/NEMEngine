#include "MotionAnimationTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/World/Systems/Animation/MotionAnimationSystem.h>
#include <Engine/Core/World/Systems/Animation/SimpleAnimationSystem.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Scripting/Managed/ManagedMotionAnimationSettings.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
	void Require(bool condition, const char* message) {
		if (!condition) throw std::runtime_error(message);
	}
	bool Near(float a, float b) { return std::abs(a - b) < 0.0002f; }
}

bool NEMTests::TestMotionAnimation() {
	using namespace Engine;
	try {
		RegisterBuiltinAnimationProperties();
		{
			ECSWorld conflictWorld;
			const Entity owner = conflictWorld.CreateEntity();
			conflictWorld.AddComponent<TransformComponent>(owner);
			conflictWorld.AddComponent<SimpleAnimationComponent>(owner);
			conflictWorld.AddComponent<MotionAnimationComponent>(owner);
			auto& simple = conflictWorld.GetComponent<SimpleAnimationComponent>(owner);
			simple.playOnStart = false;
			simple.autoApply = true;
			simple.settings = MakeSimpleAnimationSettings(2);
			simple.settings.end = Vector3(10, 0, 0);
			simple.binding = {"Transform", "localPos", AnimationValueType::Vector3};
			auto* motion = TryGetMotionAnimation(conflictWorld, owner);
			motion->settings.playOnStart = false;
			motion->settings.duration = 10;
			motion->settings.offset = {100, 0, 0};
			SystemContext frame;
			frame.mode = WorldMode::Play;
			frame.deltaTime = frame.unscaledDeltaTime = 0.25f;
			SimpleAnimationSystem simpleSystem;
			MotionAnimationSystem motionSystem;
			simpleSystem.Update(conflictWorld, frame);
			motionSystem.Update(conflictWorld, frame);
			simple.runtimeRequest = 6;
			simple.runtimeSeekTime = 0.5f;
			simpleSystem.Update(conflictWorld, frame);
			motion->runtime.request = 1;
			motionSystem.Update(conflictWorld, frame);
			Require(simple.runtimeApplied && Near(conflictWorld.GetComponent<TransformComponent>(owner).localPos.x, 5) &&
				!motion->runtime.diagnostic.empty(), "停止中のSimple Seekも競合として扱う");
			simple.runtimeRequest = 4;
			simpleSystem.Update(conflictWorld, frame);
			Require(!simple.runtimeApplied, "書込みのない停止frameへフラグを持ち越さない");
			simple.runtimeRequest = 5;
			simpleSystem.Update(conflictWorld, frame);
			motionSystem.Update(conflictWorld, frame);
			Require(simple.runtimeApplied && Near(conflictWorld.GetComponent<TransformComponent>(owner).localPos.x, 0), "Simple Resetも競合として扱う");
			simple.runtimeRequest = 1;
			simpleSystem.Update(conflictWorld, frame);
			for (int i = 0; i < 5; ++i) simpleSystem.Update(conflictWorld, frame);
			motionSystem.Update(conflictWorld, frame);
			Require(simple.runtimeFinished && !simple.runtimePlaying && simple.runtimeApplied &&
				Near(conflictWorld.GetComponent<TransformComponent>(owner).localPos.x, 10), "Simple完了後の最終値も上書きしない");
			Require(!nlohmann::json(simple).contains("runtimeApplied"), "書込フラグは保存しない");
			simple.enabled = false;
			simpleSystem.Update(conflictWorld, frame);
			Require(!simple.runtimeApplied, "無効化したframeへフラグを持ち越さない");
		}
		{
			MotionAnimationSettings rotationSettings;
			rotationSettings.applyRotation = true;
			for (const auto euler : {Vector3(0, 90, 0), Vector3(23, -42, 67), Vector3(0, 0, 0)}) {
				rotationSettings.rotationEdit = SimpleAnimationRotationMode::Euler;
				rotationSettings.euler = euler;
				const auto expected = Quaternion::FromEulerDegrees(euler);
				SetMotionAnimationRotationEdit(rotationSettings, SimpleAnimationRotationMode::AxisAngle);
				Require(Quaternion::NearlyEqual(EvaluateMotionAnimation(rotationSettings, {}, 1).rotation, expected),
					"オイラーから軸と角度へ切替えても回転を保持");
				SetMotionAnimationRotationEdit(rotationSettings, SimpleAnimationRotationMode::Euler);
				Require(Quaternion::NearlyEqual(EvaluateMotionAnimation(rotationSettings, {}, 1).rotation, expected),
					"軸と角度からオイラーへ切替えても回転を保持");
			}
		}
		MotionAnimationSettings settings;
		settings.offset = {0, 0, 4};
		MotionAnimationRuntime runtime;
		runtime.origin = {1, 2, 3}; runtime.originRotation = Quaternion::FromEulerDegrees({0, 90, 0});
		auto sample = EvaluateMotionAnimation(settings, runtime, 0.5);
		Require(Near((sample.position - runtime.origin).Length(), 2), "開始姿勢から相対距離を補間");
		settings.offset = {0, 10, 4}; settings.plane = MotionAnimationPlane::GroundXZ;
		Require(Near(EvaluateMotionAnimation(settings, runtime, 1).position.y, 2), "地面移動は開始時の高さを保つ");
		settings.plane = MotionAnimationPlane::PlaneXY;
		Require(Near(EvaluateMotionAnimation(settings, runtime, 1).position.z, 3), "2D移動はZを保つ");
		settings = {}; settings.mode = MotionAnimationMode::ApproachTarget; settings.stoppingDistance = 2;
		runtime = {}; runtime.targetPosition = {0, 0, 10};
		Require(Near(EvaluateMotionAnimation(settings, runtime, 1).position.z, 8), "対象の手前で停止");
		runtime.targetPosition = {0, 0, 1};
		Require(Near(EvaluateMotionAnimation(settings, runtime, 1).position.z, 0), "停止距離内では逆走しない");
		settings = {}; settings.mode = MotionAnimationMode::Spline; settings.delay = 1;
		runtime = {}; runtime.splineSamples = {{{10, 0, 0}, 0, {1, 0, 0}}, {{14, 0, 0}, 4, {1, 0, 0}}};
		Require(Near(EvaluateMotionAnimation(settings, runtime, 0.5).position.Length(), 0), "Splineも開始遅延中は原点を保つ");
		settings = {}; settings.applyRotation = true; settings.euler = {0, 720, 0};
		const auto rotation = EvaluateMotionAnimation(settings, runtime, 0.125).rotation;
		Require(Quaternion::NearlyEqual(rotation, Quaternion::FromEulerDegrees({0, 90, 0})), "オイラーの複数回転");
		settings = {}; settings.applyRotation = true; settings.rotation = MotionAnimationRotation::LookAtTarget;
		runtime = {}; runtime.targetPosition = {10, 0, 0};
		Require(Quaternion::NearlyEqual(EvaluateMotionAnimation(settings, runtime, 0).rotation, Quaternion::Identity()), "注視回転の開始姿勢");
		const auto targetRotation = Quaternion::LookRotation(runtime.targetPosition.Normalize(), {0, 1, 0});
		settings.offset = {};
		Require(Quaternion::NearlyEqual(EvaluateMotionAnimation(settings, runtime, 0.5).rotation,
			Quaternion::Lerp(Quaternion::Identity(), targetRotation, 0.5f).Normalize()), "注視回転の中間補間");
		Require(Quaternion::NearlyEqual(EvaluateMotionAnimation(settings, runtime, 1).rotation, targetRotation), "注視回転の到達姿勢");
		settings = {}; settings.mode = MotionAnimationMode::Segments;
		settings.segments = {{{1, 0, 0}, 1}, {{1, 0, 2}, 1}, {{0, 0, 3}, 1}};
		runtime = {};
		Require(Near(EvaluateMotionAnimation(settings, runtime, 1.5).position.z, 1), "区間ごとの終点と時間");
		settings.repeat = SimpleAnimationRepeat::Loop;
		AccumulateMotionAnimationEvents(settings, runtime, 0, 10);
		Require(runtime.pendingSegments == std::vector<uint64_t>({4, 3, 3}) && runtime.pendingCycles == 3, "複数周期の区間通知を失わない");
		settings.repeat = SimpleAnimationRepeat::PingPong; settings.cycleCount = 2; runtime = {};
		AccumulateMotionAnimationEvents(settings, runtime, 0, 15);
		Require(runtime.pendingSegments == std::vector<uint64_t>({4, 4, 4}) && runtime.pendingCycles == 2 && runtime.pendingCompletions == 1,
			"往復通知と有限回数の完了");
		Require(Near(EvaluateMotionAnimation(settings, runtime, 15).position.Length(), 0), "往復の終了位置");
		nlohmann::json saved = settings;
		Require(nlohmann::json(saved.get<MotionAnimationSettings>()) == saved && !saved.contains("runtime"), "設定の保存と再読込");
		bool rejected = false;
		try { saved["duration"] = -1; (void)saved.get<MotionAnimationSettings>(); } catch (...) { rejected = true; }
		Require(rejected, "不正な時間を拒否");

		// C#の追加直後は予約領域へ設定と再生操作を書き込み、公開時に所有を移す
		for (int pendingMode = 0; pendingMode < 3; ++pendingMode) {
			ECSWorld pendingWorld;
			const Entity pendingOwner = pendingWorld.CreateEntity();
			pendingWorld.AddComponent<TransformComponent>(pendingOwner);
			const auto typeID = ComponentTypeRegistry::GetInstance().GetID<MotionAnimationComponent>();
			const auto instanceID = pendingWorld.GetCommandBuffer().StageAddComponent(pendingWorld, pendingOwner, typeID);
			Require(instanceID != 0 && !pendingWorld.HasComponent<MotionAnimationComponent>(pendingOwner), "Motionの追加を予約");
			nlohmann::json pendingSettings;
			Require(ReadMotionAnimationSettings(pendingWorld, pendingOwner, pendingSettings), "追加予約直後に既定設定を取得");
			Require(ApplyMotionAnimationSettings(pendingWorld, pendingOwner, {{"duration", 2.0}, {"offset", Vector3(8, 0, 0).ToJson()}}),
				"追加予約直後に設定を更新");
			Require(ApplyMotionAnimationSettings(pendingWorld, pendingOwner, {{"command", 1}}), "追加予約直後に再生を指定");
			auto& storage = pendingWorld.GetStorage().Get<MotionAnimationStorage>();
			Require(storage.GetAliveCount() == 1, "予約中の外部領域は一つだけ");
			if (pendingMode == 0) pendingWorld.GetCommandBuffer().Clear();
			else if (pendingMode == 1) {
				pendingWorld.FlushWorldCommands();
				const auto* pendingRecord = TryGetMotionAnimation(pendingWorld, pendingOwner);
				Require(pendingRecord && Near(pendingRecord->settings.offset.x, 8) && pendingRecord->runtime.request == 1,
					"公開後も設定と再生要求を維持");
				Require(storage.GetAliveCount() == 1, "公開時に外部領域を二重生成しない");
				MotionAnimationSystem pendingSystem;
				SystemContext pendingContext;
				pendingContext.mode = WorldMode::Play;
				pendingContext.deltaTime = pendingContext.unscaledDeltaTime = 0.5f;
				pendingSystem.Update(pendingWorld, pendingContext);
				pendingSystem.Update(pendingWorld, pendingContext);
				Require(Near(pendingWorld.GetComponent<TransformComponent>(pendingOwner).localPos.x, 2), "予約した設定で再生");
				pendingWorld.RemoveComponent<MotionAnimationComponent>(pendingOwner);
			} else {
				pendingWorld.GetCommandBuffer().EnqueueRemoveComponentByName(pendingOwner, "MotionAnimation");
				pendingWorld.FlushWorldCommands();
			}
			Require(storage.GetAliveCount() == 0, "取消と削除で外部領域を解放");
		}

		{
			ECSWorld referenceWorld;
			const Entity owner = referenceWorld.CreateEntity();
			referenceWorld.AddComponent<MotionAnimationComponent>(owner);
			const Entity candidate = referenceWorld.CreateEntity();
			auto& registry = ManagedWorldRegistry::GetInstance();
			const auto handle = registry.Register(referenceWorld);
			const auto native = MakeNativeEntity(referenceWorld, candidate);
			const nlohmann::json reference = {{"world", {{"index", native.world.index}, {"generation", native.world.generation}}},
				{"index", native.index}, {"generation", native.generation}};
			Require(!ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", reference}}),
				"Splineを持たないオブジェクトを拒否");
			const auto typeID = ComponentTypeRegistry::GetInstance().GetID<SplineComponent>();
			Require(referenceWorld.GetCommandBuffer().StageAddComponent(referenceWorld, candidate, typeID) != 0,
				"Splineの追加を予約");
			Require(ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", reference}}),
				"追加予約直後のSplineをC#から参照");
			referenceWorld.FlushWorldCommands();
			Require(ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", reference}}),
				"公開後のSplineをC#から参照");
			referenceWorld.RemoveComponent<SplineComponent>(candidate);
			Require(!ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", reference}}),
				"Spline削除後の参照設定を拒否");
			referenceWorld.AddComponent<SplineComponent>(candidate);
			referenceWorld.DestroyEntity(candidate);
			Require(!ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", reference}}),
				"破棄予約されたSplineの参照設定を拒否");
			const ManagedNativeEntity none{};
			const nlohmann::json nullReference = {{"world", {{"index", none.world.index}, {"generation", none.world.generation}}},
				{"index", none.index}, {"generation", none.generation}};
			Require(ApplyMotionAnimationSettings(referenceWorld, owner, {{"splineNative", nullReference}}) &&
				!TryGetMotionAnimation(referenceWorld, owner)->runtime.hasSplineOverride, "Spline参照の解除は許可");
			registry.Unregister(handle);
		}

		ECSWorld world;
		const Entity object = world.CreateEntity();
		world.AddComponent<TransformComponent>(object); world.AddComponent<MotionAnimationComponent>(object);
		MotionAnimationSystem system;
		SystemContext context; context.mode = WorldMode::Play; context.deltaTime = context.unscaledDeltaTime = 0.25f;
		auto* record = TryGetMotionAnimation(world, object);
		record->settings.offset = {4, 0, 0};
		system.Update(world, context); system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 1), "Worldへの移動適用");
		record->runtime.request = 2; system.Update(world, context);
		Require(Near(static_cast<float>(record->runtime.time), 0.25f), "Pauseで時刻を保つ");
		record->runtime.request = 6; record->runtime.seekTime = 1; system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 4) && record->runtime.pendingCompletions == 0, "Seekは評価だけ行う");
		record->runtime.request = 5; system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 0), "Resetは原点へ戻す");
		record->settings.autoApply = false; record->runtime.request = 1;
		system.Update(world, context); system.Update(world, context);
		Require(Near(record->runtime.deltaPosition.x, 1) && Near(world.GetComponent<TransformComponent>(object).localPos.x, 0), "結果のみ公開");
		record->settings.autoApply = true;
		const Entity target = world.CreateEntity(); world.AddComponent<TransformComponent>(target);
		world.GetComponent<TransformComponent>(target).localPos = {0, 0, 10};
		record->settings.mode = MotionAnimationMode::ApproachTarget; record->settings.trackTarget = true;
		record->runtime.targetOverride = target; record->runtime.hasTargetOverride = true; record->runtime.request = 1;
		system.Update(world, context); system.Update(world, context);
		const Vector3 paused = record->runtime.position;
		record->runtime.request = 2; world.GetComponent<TransformComponent>(target).localPos = {0, 0, 20}; system.Update(world, context);
		Require(Near(record->runtime.position.z, paused.z), "Pause中は追跡対象が動いてもPoseを保つ");
		record->runtime.request = 3; context.deltaTime = 0; system.Update(world, context);
		Require(Near(record->runtime.position.z, paused.z), "時間倍率0では追跡も停止");
		context.deltaTime = 0.25f; record->runtime.request = 8; system.Update(world, context);
		Require(record->runtime.frozenTarget && Near(record->runtime.targetPosition.z, 10), "対象の固定");
		record->runtime.request = 1; system.Update(world, context);
		Require(!record->runtime.frozenTarget && Near(record->runtime.targetPosition.z, 20), "Playは対象を取り直す");
		world.DestroyEntity(target); system.Update(world, context);
		Require(!record->runtime.playing && !record->runtime.diagnostic.empty(), "対象消失で停止");
		world.FlushPendingDestroyEntities();
		system.Update(world, context); Require(!record->runtime.diagnostic.empty(), "停止原因を維持");

		world.GetComponent<TransformComponent>(object).localPos = {};
		record->runtime = {}; record->settings = {}; record->settings.offset = {4, 0, 0};
		world.AddComponent<TimeScaleComponent>(object);
		world.GetComponent<TimeScaleComponent>(object).timeScale = 0.5f;
		system.Update(world, context); system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 0.5f), "ローカル時間を一度だけ適用");
		world.GetComponent<TimeScaleComponent>(object).timeScale = 1;
		world.AddComponent<SimpleAnimationComponent>(object);
		auto& simple = world.GetComponent<SimpleAnimationComponent>(object);
		simple.autoApply = true; simple.runtimePlaying = true;
		simple.binding = {"Transform", "localPos", AnimationValueType::Vector3};
		const float conflictStart = world.GetComponent<TransformComponent>(object).localPos.x;
		system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, conflictStart) && !record->runtime.diagnostic.empty(), "競合チャネルへの上書きを停止");
		record->runtime.request = 5; system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, conflictStart), "Resetでも競合先を上書きしない");
		world.RemoveComponent<SimpleAnimationComponent>(object);
		const Entity path = world.CreateEntity(); world.AddComponent<TransformComponent>(path); world.AddComponent<SplineComponent>(path);
		SplineData spline; spline.points = {{Vector3(0, 0, 0)}, {Vector3(4, 0, 0)}};
		SetSpline(world, path, spline);
		record->settings.mode = MotionAnimationMode::Spline; record->settings.splineEnd = 4;
		record->runtime.splineOverride = path; record->runtime.hasSplineOverride = true; record->runtime.request = 1;
		system.Update(world, context); system.Update(world, context);
		Require(Near(record->runtime.position.x, 1), "動的Splineの距離評価");
		const Entity secondPath = world.CreateEntity(); world.AddComponent<TransformComponent>(secondPath); world.AddComponent<SplineComponent>(secondPath);
		world.GetComponent<TransformComponent>(secondPath).localPos = {0, 0, 10}; SetSpline(world, secondPath, spline);
		auto& registry = ManagedWorldRegistry::GetInstance();
		const auto handle = registry.Register(world);
		const auto nativePath = MakeNativeEntity(world, secondPath);
		const nlohmann::json nativeData = {{"world", {{"index", nativePath.world.index}, {"generation", nativePath.world.generation}}},
			{"index", nativePath.index}, {"generation", nativePath.generation}};
		const bool changed = ApplyMotionAnimationSettings(world, object, {{"splineNative", nativeData}});
		registry.Unregister(handle);
		Require(changed, "C#経由のSpline変更");
		system.Update(world, context);
		Require(Near(record->runtime.position.z, 10), "再生中のSpline変更は次の評価に反映");
		auto changedSettings = record->settings;
		changedSettings.spline.localFileID = world.GetUUID(path);
		changedSettings.target.localFileID = world.GetUUID(path);
		record->runtime.hasTargetOverride = true;
		SetMotionAnimationSettings(world, object, changedSettings);
		Require(!record->runtime.hasSplineOverride && !record->runtime.hasTargetOverride && record->runtime.splineSamples.empty(),
			"Inspector参照変更で古いC#参照を解除");
		record->runtime.hasSplineOverride = record->runtime.hasTargetOverride = true;
		changedSettings.spline = {}; changedSettings.target = {};
		MotionAnimationComponent::DeserializeECS(world, object, changedSettings, world.GetComponent<MotionAnimationComponent>(object));
		Require(!record->runtime.hasSplineOverride && !record->runtime.hasTargetOverride, "Undoの参照変更でも実行時参照を解除");
		record->settings.mode = MotionAnimationMode::Relative;
		changedSettings = record->settings; changedSettings.mode = MotionAnimationMode::Spline;
		SetMotionAnimationSettings(world, object, changedSettings);
		record->runtime.hasSplineOverride = true; record->runtime.splineOverride = secondPath;
		system.Update(world, context);
		Require(!record->runtime.splineSamples.empty() && Near(record->runtime.position.z, 10), "再生中の移動方式変更で経路を構築");
		world.DestroyEntity(secondPath);
		system.Update(world, context);
		Require(!record->runtime.playing && !record->runtime.diagnostic.empty(), "Splineの破棄予約でも停止");
		world.FlushPendingDestroyEntities();
		const auto copied = world.CloneForSerialization();
		const auto* cloned = TryGetMotionAnimation(*copied, copied->FindByUUID(world.GetUUID(object)));
		Require(cloned && !cloned->runtime.initialized && cloned->settings.mode == MotionAnimationMode::Spline, "複製は設定のみ引き継ぐ");

		world.GetComponent<TransformComponent>(object).localPos = {};
		record->runtime = {}; record->settings = {}; record->settings.offset = {4, 0, 0};
		record->settings.playInEditMode = true; context.mode = WorldMode::Edit;
		system.Update(world, context); system.Update(world, context);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 1), "編集プレビュー");
		world.RemoveComponent<MotionAnimationComponent>(object);
		Require(Near(world.GetComponent<TransformComponent>(object).localPos.x, 0), "削除時に編集プレビューを復元");
		std::cout << "MotionAnimation: sampling / events / playback / target / preview passed\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "MotionAnimation: " << error.what() << '\n'; return false;
	}
}
