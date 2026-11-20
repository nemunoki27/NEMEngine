#include "TimelineBuiltinTests.h"
#include "ParticleGPUCheckpointTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinEvaluation.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayback.h>
#include <Engine/Core/Audio/AudioPlaybackCursor.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Rendering/BeamRendererSystem.h>
#include <Engine/Core/World/Systems/Rendering/TrailRendererSystem.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	bool Near(float first, float second) {

		return std::abs(first - second) < 0.0002f;
	}

	// 重複Clipと終了処理の順番に左右されず同じ値へ復元する
	void CheckPropertyMixing() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.GetComponent<TransformComponent>(actor).localPos.x = 2;
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::Property;
		TimelineClip first, second;
		first.data = second.data = CreateDefaultTimelineClipData(track.kind);
		first.data["to"] = Vector3(10, 0, 0).ToJson();
		second.data["to"] = Vector3(20, 0, 0).ToJson();
		SystemContext systems;
		const TimelineControlOwner firstOwner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		const TimelineControlOwner secondOwner{firstOwner.player, Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext firstContext{world, systems, actor, actor, asset, track, first, {}, firstOwner};
		TimelineEvaluationContext secondContext{world, systems, actor, actor, asset, track, second, {}, secondOwner};
		firstContext.normalizedTime = secondContext.normalizedTime = 1;
		firstContext.duration = secondContext.duration = first.duration;
		firstContext.localTime = secondContext.localTime = first.duration;
		firstContext.weight = secondContext.weight = 0.5f;
		std::string diagnostic;
		auto firstInstance = CreateBuiltinTimelineClip(track, first, diagnostic);
		auto secondInstance = CreateBuiltinTimelineClip(track, second, diagnostic);
		Require(firstInstance && secondInstance, "Property Clipを構築できる");
		Require(firstInstance->Enter(firstContext, diagnostic) && secondInstance->Enter(secondContext, diagnostic), "Property Clipを開始できる");
		BeginBuiltinTimelineEvaluation(world);
		firstContext.normalizedTime = 0.5f;
		firstContext.localTime = first.duration * 0.5;
		firstInstance->Evaluate(firstContext);
		firstContext.normalizedTime = 1;
		firstContext.localTime = first.duration;
		firstInstance->Evaluate(firstContext);
		secondInstance->Evaluate(secondContext);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 2), "混合完了までは対象を書き換えない");
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 15), "重なったClipを重みで正規化する");
		firstInstance->Exit(firstContext, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, firstOwner);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 15), "別Clipの制御中には復元しない");
		secondInstance->Exit(secondContext, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, secondOwner);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 2), "最後のClipは最初の共通値へ復元する");
		world.GetComponent<TransformComponent>(actor).localPos.x = 7;
		Require(firstInstance->Enter(firstContext, diagnostic), "再生後に変更した値を再取得できる");
		BeginBuiltinTimelineEvaluation(world);
		firstInstance->Evaluate(firstContext);
		DeferBuiltinTimelineExit(world, [&] {
			firstInstance->Exit(firstContext, TimelineExitReason::Completed);
			ReleaseTimelineControls(world, firstOwner);
		});
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 7), "完了フレームの書込み後に復元する");
	}

	// 実行時の判定終了とEditorの元値復元を分ける
	void CheckCollisionWindows() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<CollisionComponent>(actor);
		world.GetComponent<CollisionComponent>(actor).enabled = false;
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::Collision;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		SystemContext systems;
		const TimelineControlOwner owner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext context{world, systems, actor, actor, asset, track, clip, {}, owner};
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance->Enter(context, diagnostic), "判定区間を開始できる");
		instance->Evaluate(context);
		Require(world.GetComponent<CollisionComponent>(actor).enabled, "判定区間中はCollisionが有効になる");
		instance->Exit(context, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, owner);
		Require(!world.GetComponent<CollisionComponent>(actor).enabled, "停止時に攻撃判定を残さない");
		world.GetComponent<CollisionComponent>(actor).enabled = true;
		context.mode = TimelineEvaluationMode::Preview;
		Require(instance->Enter(context, diagnostic), "判定プレビューを開始できる");
		instance->Evaluate(context);
		instance->Exit(context, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, owner);
		Require(world.GetComponent<CollisionComponent>(actor).enabled, "Editorでは元の有効状態へ戻す");
	}

	// 時間停止中も実時間の解除だけは進む
	void CheckHitStop() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TimeScaleComponent>(actor);
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::TimeScale;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		clip.data["hitStop"] = true;
		clip.data["holdSeconds"] = 0.05f;
		SystemContext systems;
		const TimelineControlOwner owner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext context{world, systems, actor, actor, asset, track, clip, {}, owner};
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance->Enter(context, diagnostic), "ヒットストップを開始できる");
		instance->Evaluate(context);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0), "対象だけを停止する");
		UpdateBuiltinTimelineRealtime(world, 0.02f);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0), "指定時間までは停止を維持する");
		UpdateBuiltinTimelineRealtime(world, 0.04f);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 1), "実時間で停止を解除する");
		instance->Evaluate(context);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 1), "同じ区間内で停止を繰り返さない");
		instance->Exit(context, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, owner);

		TimelineClip longer = clip;
		longer.data["holdSeconds"] = 0.1f;
		const TimelineControlOwner longerOwner{owner.player, Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext longerContext{world, systems, actor, actor, asset, track, longer, {}, longerOwner};
		auto longerInstance = CreateBuiltinTimelineClip(track, longer, diagnostic);
		Require(instance->Enter(context, diagnostic), "短い停止区間を再生できる");
		instance->Evaluate(context);
		Require(longerInstance->Enter(longerContext, diagnostic), "長い停止区間を重ねられる");
		longerInstance->Evaluate(longerContext);
		UpdateBuiltinTimelineRealtime(world, 0.06f);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0), "短い停止が終わっても長い停止を維持する");
		UpdateBuiltinTimelineRealtime(world, 0.05f);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 1), "最後の停止だけが元の倍率へ戻す");
		instance->Exit(context, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, owner);
		longerInstance->Exit(longerContext, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, longerOwner);
		TimelineClip resume = clip;
		resume.data["hitStop"] = false;
		resume.data["scale"] = 0.5f;
		TimelineEvaluationContext resumeContext{world, systems, actor, actor, asset, track, resume, {}, longerOwner};
		auto resumeInstance = CreateBuiltinTimelineClip(track, resume, diagnostic);
		Require(instance->Enter(context, diagnostic), "停止を再開始できる");
		instance->Evaluate(context);
		instance->Retire(context);
		Require(resumeInstance->Enter(resumeContext, diagnostic), "停止直後の倍率設定を開始できる");
		BeginBuiltinTimelineEvaluation(world);
		resumeInstance->Evaluate(resumeContext);
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0.5f), "退役した停止は次の倍率を上書きしない");
		instance->Exit(context, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, owner);
		resumeInstance->Exit(resumeContext, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, longerOwner);
	}

	// 全体停止を発生させたActorが消えても時計を再開する
	void CheckDestroyedHitStopOwner() {

		const float original = ManagedScriptRuntime::GetRuntimeTimeScale();
		ManagedScriptRuntime::SetRuntimeTimeScale(1);
		ECSWorld world;
		const Entity actor = world.CreateEntity();
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::TimeScale;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		clip.data["global"] = clip.data["hitStop"] = true;
		SystemContext systems;
		const TimelineControlOwner owner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext context{world, systems, actor, actor, asset, track, clip, {}, owner};
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance->Enter(context, diagnostic), "全体停止を開始できる");
		instance->Evaluate(context);
		Require(Near(ManagedScriptRuntime::GetRuntimeTimeScale(), 0), "全体時計を停止する");
		world.DestroyEntity(actor);
		UpdateBuiltinTimelineRealtime(world, 0.2f);
		Require(Near(ManagedScriptRuntime::GetRuntimeTimeScale(), 1), "Actor消滅後も停止を解除する");
		instance->Exit(context, TimelineExitReason::WorldExit);
		ReleaseTimelineControls(world, owner);
		ManagedScriptRuntime::SetRuntimeTimeScale(original);
	}

	// 移動の終点保持と2Dカメラの復元を確認する
	void CheckMotionAndCamera() {

		ECSWorld world;
		const Entity actor = world.CreateEntity(), shot = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TransformComponent>(shot);
		world.GetComponent<TransformComponent>(actor).localPos = {7, 0, 0};
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::Motion;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		MotionAnimationSettings settings;
		settings.offset = {0, 0, 4};
		clip.data["settings"] = settings;
		SystemContext systems;
		const TimelineControlOwner owner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext context{world, systems, actor, actor, asset, track, clip, {}, owner};
		context.normalizedTime = 1;
		context.duration = context.localTime = clip.duration;
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance->Enter(context, diagnostic), "相対移動を開始できる");
		BeginBuiltinTimelineEvaluation(world);
		instance->Evaluate(context);
		CommitBuiltinTimelineSamples(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 7) &&
			Near(world.GetComponent<TransformComponent>(actor).localPos.z, 4), "開始姿勢を基準に絶対時刻で移動する");
		FlushBuiltinTimelineEvaluation(world, systems);
		instance->Exit(context, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, owner);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.z, 4), "完了した移動位置を保持する");

		world.AddComponent<OrthographicCameraComponent>(actor);
		world.GetComponent<OrthographicCameraComponent>(actor).common.priority = 7;
		world.GetComponent<TransformComponent>(shot).localPos = {20, 2, 3};
		world.GetComponent<TransformComponent>(shot).localScale = {2, 2, 2};
		track.kind = TimelineTrackKind::Camera;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		clip.data["shotBinding"] = "Shot";
		const TimelineResolvedBinding binding{Engine::UUID::New(), "Shot", shot};
		context.bindings = {&binding, 1};
		context.mode = TimelineEvaluationMode::Preview;
		instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance->Enter(context, diagnostic), "2Dカメラ演出を開始できる");
		BeginBuiltinTimelineEvaluation(world);
		instance->Evaluate(context);
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 20) &&
			Near(world.GetComponent<TransformComponent>(actor).localScale.x, 2), "2Dカメラの座標とズームを反映する");
		instance->Exit(context, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, owner);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 7) &&
			Near(world.GetComponent<TransformComponent>(actor).localScale.x, 1) &&
			world.GetComponent<OrthographicCameraComponent>(actor).common.priority == 7, "カメラ演出後に元の状態へ戻す");
	}

	// 同じEmitterを重ねず、プレビューの停止状態を正確に復元する
	void CheckParticleLeases() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<ParticleSystemComponent>(actor);
		auto settings = MakeDefaultParticleSystemSettings();
		settings.phases.front().modules.clear();
		SetParticleSystemSettings(world, actor, settings);
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::Particle;
		TimelineClip first, second;
		first.duration = second.duration = 1;
		first.data = second.data = CreateDefaultTimelineClipData(track.kind);
		SystemContext systems;
		const TimelineControlOwner firstOwner{Engine::UUID::New(), Engine::UUID::New(), 0, 1};
		const TimelineControlOwner secondOwner{firstOwner.player, Engine::UUID::New(), 0, 1};
		TimelineEvaluationContext firstContext{world, systems, actor, actor, asset, track, first, {}, firstOwner};
		TimelineEvaluationContext secondContext{world, systems, actor, actor, asset, track, second, {}, secondOwner};
		firstContext.mode = secondContext.mode = TimelineEvaluationMode::Preview;
		std::string diagnostic;
		auto firstInstance = CreateBuiltinTimelineClip(track, first, diagnostic);
		auto secondInstance = CreateBuiltinTimelineClip(track, second, diagnostic);
		Require(firstInstance->Enter(firstContext, diagnostic), "Particleプレビューを開始できる");
		firstContext.localTime = 0.3;
		firstInstance->Evaluate(firstContext);
		Require(firstInstance->GetPreparationProgress() == 1, "要求時刻まで同じ更新で評価する");
		for (int i = 0; i < 4 && firstInstance->GetPreparationProgress() < 1; ++i) firstInstance->Evaluate(firstContext);
		const auto* runtime = TryGetParticleSystemRuntime(world, actor);
		Require(firstInstance->GetPreparationProgress() == 1 && !runtime->instance.emitter.particles.empty(), "絶対時刻までの準備を完了できる");
		const auto count = runtime->instance.emitter.particles.size();
		const Vector3 position = runtime->instance.emitter.particles[0].pos;
		firstContext.localTime = 0.1;
		firstInstance->Evaluate(firstContext);
		firstContext.localTime = 0.3;
		for (int i = 0; i < 4; ++i) firstInstance->Evaluate(firstContext);
		runtime = TryGetParticleSystemRuntime(world, actor);
		Require(runtime->instance.emitter.particles.size() == count &&
			Near(runtime->instance.emitter.particles[0].pos.y, position.y), "巻き戻し後も同じ粒子結果を再現する");
		Require(!secondInstance->Enter(secondContext, diagnostic) && !diagnostic.empty(), "同じEmitterの重複区間を診断する");
		secondInstance->Exit(secondContext, TimelineExitReason::Faulted);
		ReleaseTimelineControls(world, secondOwner);
		second.start = 1;
		firstInstance->Retire(firstContext);
		Require(secondInstance->Enter(secondContext, diagnostic), "隣接するParticle区間は開始できる");
		firstInstance->Exit(firstContext, TimelineExitReason::Completed);
		ReleaseTimelineControls(world, firstOwner);
		secondInstance->Exit(secondContext, TimelineExitReason::Stopped);
		ReleaseTimelineControls(world, secondOwner);
		Require(TryGetParticleSystemRuntime(world, actor)->stopped, "最後の区間終了時に元の停止状態へ戻す");
		TryGetParticleSystemRuntime(world, actor)->initialized = true;
		Require(firstInstance->Enter(firstContext, diagnostic), "再作成前のComponentをプレビューする");
		world.RemoveComponent<ParticleSystemComponent>(actor);
		world.AddComponent<ParticleSystemComponent>(actor);
		firstInstance->Exit(firstContext, TimelineExitReason::BindingLost);
		ReleaseTimelineControls(world, firstOwner);
		Require(!TryGetParticleSystemRuntime(world, actor)->initialized, "再作成したComponentへ以前のRuntimeを復元しない");
	}

	// 音声を開始する前にサンプル位置へ正しく変換する
	void CheckAudioOffsets() {

		AudioSoundData sound;
		WAVEFORMATEX format{};
		format.nSamplesPerSec = 48000;
		format.nBlockAlign = 4;
		sound.formatBlob.resize(sizeof(format));
		std::memcpy(sound.formatBlob.data(), &format, sizeof(format));
		sound.pcmBuffer.resize(48000 * 4);
		Require(ResolveAudioSamplePosition(sound, 0.5, false) == 24000, "音声の途中から開始できる");
		Require(ResolveAudioSamplePosition(sound, 2, false) == 47999, "非ループ位置を末尾へ制限する");
		Require(ResolveAudioSamplePosition(sound, 2.25, true) == 12000, "ループ時はClip位置へ折り返す");
		Require(ResolveAudioSamplePosition(sound, std::numeric_limits<double>::infinity(), true) == 0, "不正な音声時刻を拒否する");
		Require(ResolveAudioSamplePosition(sound, (std::numeric_limits<double>::max)(), false) == 47999, "巨大な時刻でもサンプル位置をオーバーフローさせない");
		Require(ResolveAudioSamplePosition({}, 0, false) == 0, "未読込音声でも無効な形式を参照しない");
	}

	// 同じ失敗を毎フレーム再試行せず、再再生でだけ条件を取り直す
	void CheckPrefabFailure() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		AssetDatabase database;
		TimelineAsset asset;
		TimelineTrack track;
		track.kind = TimelineTrackKind::Prefab;
		TimelineClip clip;
		clip.data = CreateDefaultTimelineClipData(track.kind);
		clip.data["asset"] = ToAssetReferenceJson(AssetGUID::New());
		clip.data["parentBinding"] = "Parent";
		SystemContext systems;
		systems.assetDatabase = &database;
		TimelineEvaluationContext context{world, systems, actor, actor, asset, track, clip};
		std::string diagnostic;
		auto instance = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(instance && !instance->Enter(context, diagnostic) && !diagnostic.empty(), "Prefabの生成失敗を診断する");
		const auto failure = diagnostic;
		const TimelineResolvedBinding binding{Engine::UUID::New(), "Parent", actor};
		context.bindings = {&binding, 1};
		Require(!instance->Enter(context, diagnostic) && diagnostic == failure, "同じClip Instanceでは失敗時の条件を再試行しない");
		auto replay = CreateBuiltinTimelineClip(track, clip, diagnostic);
		Require(replay && !replay->Enter(context, diagnostic) && diagnostic != failure, "新しい再生Instanceでは参照先を再検証する");
	}

	// 再生基盤の区間境界で前の移動の終点を次の基準へ渡す
	void CheckPlaybackBoundaries() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		auto asset = CreateDefaultTimelineAsset("MotionChain");
		asset.duration = 2;
		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.binding = asset.bindings.front().id;
		track.kind = TimelineTrackKind::Motion;
		for (int i = 0; i < 2; ++i) {
			TimelineClip clip;
			clip.id = Engine::UUID::New();
			clip.start = i;
			clip.duration = 1;
			clip.data = CreateDefaultTimelineClipData(track.kind);
			MotionAnimationSettings settings;
			settings.offset = {0, 0, static_cast<float>(i + 2)};
			clip.data["settings"] = settings;
			track.clips.push_back(clip);
		}
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		SystemContext systems;
		Require(EvaluateTimelineAt(world, actor, player, asset, 2, systems), "連続移動の全区間を評価できる");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.z, 5), "次の移動は直前の終点を原点にする");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.z, 5), "Player停止でも確定した移動量を残す");
		world.GetComponent<TransformComponent>(actor).localPos = {};
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.5, systems, TimelineEvaluationMode::Preview), "連続移動を途中までプレビューできる");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.z, 3.5f), "次の区間も絶対時刻で補間する");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.z, 0), "プレビュー全体の最初の姿勢へ戻す");
	}

	// 子Timelineの複数境界を親Mixerの中で処理しても古いサンプルを混ぜない
	void CheckTrailEmission() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TrailRendererComponent>(actor);
		auto* trail = TryGetTrailRenderer(world, actor);
		trail->settings.lifetime = 1.0f;
		trail->settings.minVertexDistance = 0.001f;
		auto asset = CreateDefaultTimelineAsset("Trail");
		asset.duration = 2.0;
		TimelineTrack motion;
		motion.id = Engine::UUID::New();
		motion.kind = TimelineTrackKind::Property;
		motion.binding = asset.bindings.front().id;
		TimelineClip move;
		move.id = Engine::UUID::New();
		move.duration = 2.0;
		move.data = CreateDefaultTimelineClipData(motion.kind);
		move.data["to"] = Vector3(4, 0, 0).ToJson();
		motion.clips.push_back(move);
		asset.tracks.push_back(motion);
		TimelineTrack emission;
		emission.id = Engine::UUID::New();
		emission.kind = TimelineTrackKind::Trail;
		emission.binding = motion.binding;
		for (const double start : {0.2, 0.4, 1.2}) {
			TimelineClip clip;
			clip.id = Engine::UUID::New();
			clip.start = start;
			clip.duration = 0.4;
			emission.clips.push_back(clip);
		}
		asset.tracks.push_back(emission);
		TimelinePlayerRecord player;
		SystemContext systems;
		const auto scrub = [&](double time) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, systems, TimelineEvaluationMode::Preview, false, true),
				"トレイルの時刻を評価する");
		};
		scrub(0.0);
		Require(!trail->settings.emitting && trail->runtime.points.empty(), "発生前に軌跡を作らない");
		scrub(1.4);
		size_t segments = trail->runtime.points.empty() ? 0 : 1;
		for (size_t i = 1; i < trail->runtime.points.size(); ++i) if (trail->runtime.points[i].breakBefore) ++segments;
		Require(trail->settings.emitting && segments == 2 && Near(trail->runtime.points.back().root.x, 2.8f),
			"重なる区間を連結し、発生を止めた区間の前後は分離する");
		scrub(0.3);
		Require(trail->settings.emitting && !trail->runtime.points.empty() && Near(trail->runtime.points.back().root.x, 0.6f),
			"逆シークで過去の位置から軌跡を再構築する");
		const size_t held = trail->runtime.points.size();
		systems.unscaledDeltaTime = 2.0f;
		TrailRendererSystem update;
		update.LateUpdate(world, systems);
		update.LateUpdate(world, systems);
		Require(trail->runtime.points.size() == held && std::abs(trail->runtime.time - 0.3) < 0.0001,
			"一時停止中に次の描画フレームへ進んでも履歴と時刻を維持する");
		world.AddComponent<SceneObjectComponent>(actor);
		world.GetComponent<SceneObjectComponent>(actor).activeInHierarchy = false;
		scrub(0.5);
		Require(trail->runtime.points.empty(), "無効な対象の軌跡はシークでも表示しない");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(trail->settings.emitting && trail->runtime.points.empty(), "終了時にプレビュー前の発生設定と履歴を復元する");
		world.GetComponent<SceneObjectComponent>(actor).activeInHierarchy = true;
		systems.mode = WorldMode::Play;
		systems.deltaTime = systems.unscaledDeltaTime = 1.0f / 60.0f;
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.7, systems, TimelineEvaluationMode::Playback) && trail->settings.emitting && !trail->runtime.timelineHoldsHistory,
			"再生中は重なった片方の区間が終了しても発生を継続する");
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.0, systems, TimelineEvaluationMode::Playback) && !trail->settings.emitting,
			"最後の区間を抜けると発生を止める");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(trail->settings.emitting, "再生終了時に元の発生設定を復元する");
	}

	// 逆シークと重複区間で稲妻の形状と再生時刻を再現する
	void CheckBeamEmission() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<BeamRendererComponent>(actor);
		auto& beam = *TryGetBeamRenderer(world, actor);
		beam.settings.mode = BeamMode::Lightning;
		beam.settings.playbackMode = BeamPlaybackMode::OneShot;
		beam.settings.duration = 0.05f;
		PlayBeam(world, actor);
		beam.runtime.time = 0.125;
		auto asset = CreateDefaultTimelineAsset("Beam");
		TimelineTrack motion;
		motion.id = Engine::UUID::New();
		motion.kind = TimelineTrackKind::Property;
		motion.binding = asset.bindings.front().id;
		TimelineClip move;
		move.id = Engine::UUID::New();
		move.duration = 2.0;
		move.data = CreateDefaultTimelineClipData(motion.kind);
		move.data["to"] = Vector3(2, 0, 0).ToJson();
		motion.clips.push_back(move);
		asset.tracks.push_back(motion);
		TimelineTrack emission;
		emission.id = Engine::UUID::New();
		emission.kind = TimelineTrackKind::Beam;
		emission.binding = motion.binding;
		TimelineClip clip;
		clip.id = Engine::UUID::New();
		clip.start = 0.25;
		clip.duration = 0.75;
		clip.clipIn = 0.1;
		clip.speed = 2.0;
		emission.clips.push_back(clip);
		clip.id = Engine::UUID::New();
		clip.start = 0.5;
		emission.clips.push_back(clip);
		asset.tracks.push_back(emission);
		TimelinePlayerRecord player;
		SystemContext systems;
		const auto scrub = [&](double time) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, systems, TimelineEvaluationMode::Preview, false, true),
				"ビームを指定時刻で評価する");
		};
		scrub(0);
		Require(!beam.runtime.playing && beam.runtime.points.empty(), "区間外のビームを消す");
		scrub(0.75);
		Require(beam.runtime.playing && beam.runtime.timelineControlled && std::abs(beam.runtime.time - 0.6) < 1e-8,
			"重複したビーム区間は後発側の開始位置と速度で評価する");
		const auto expected = beam.runtime.points;
		Require(2 < expected.size() && Near(expected.front().position.x, 0.75f), "同じ時刻の移動と稲妻を反映する");
		scrub(1.25);
		Require(!beam.runtime.playing && beam.runtime.points.empty(), "ビームの終端で表示を止める");
		scrub(0.75);
		systems.deltaTime = systems.unscaledDeltaTime = 1.0f;
		BeamRendererSystem update;
		update.LateUpdate(world, systems);
		Require(beam.runtime.points.size() == expected.size() && std::abs(beam.runtime.time - 0.6) < 1e-8,
			"シーク後の描画更新で単発ビームの時刻を進めない");
		for (size_t i = 0; i < expected.size(); ++i)
			Require(beam.runtime.points[i].position == expected[i].position, "同じ時刻への逆シークで稲妻の分岐も一致する");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(!beam.runtime.timelineControlled && beam.runtime.playing && beam.runtime.time == 0.125,
			"プレビュー前のビーム再生状態を復元する");
		systems.mode = WorldMode::Play;
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.75, systems, TimelineEvaluationMode::Playback), "ビームをゲーム中に再生する");
		update.LateUpdate(world, systems);
		Require(beam.runtime.playing && !beam.runtime.points.empty(), "再生中は単発ビームも区間に従う");
		ReleaseTimelinePlayer(world, actor, player, systems);
	}

	void CheckNestedPlaybackMixing() {

		ECSWorld world;
		const Entity actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.GetComponent<TransformComponent>(actor).localPos.x = 3;
		auto asset = CreateDefaultTimelineAsset("NestedProperties");
		asset.duration = 2;
		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.binding = asset.bindings.front().id;
		track.kind = TimelineTrackKind::Property;
		for (int i = 0; i < 2; ++i) {
			TimelineClip clip;
			clip.id = Engine::UUID::New();
			clip.start = i == 0 ? 0 : 0.5;
			clip.duration = i == 0 ? 1 : 1.5;
			clip.data = CreateDefaultTimelineClipData(track.kind);
			clip.data["from"] = Vector3(i == 0 ? 0.0f : 20.0f, 0, 0).ToJson();
			clip.data["to"] = Vector3(i == 0 ? 10.0f : 40.0f, 0, 0).ToJson();
			track.clips.push_back(clip);
		}
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		SystemContext systems;
		BeginBuiltinTimelineEvaluation(world);
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.25, systems), "親Mixer内で子の最初の時刻を評価する");
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.75, systems), "親Mixer内で同じ子を更新する");
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, (7.5f + 20.0f + 20.0f / 6) * 0.5f),
			"過去のサンプルを置換し、現在重なっているClipだけ混合する");
		BeginBuiltinTimelineEvaluation(world);
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.5, systems), "完了したClipを越えて子を評価する");
		FlushBuiltinTimelineEvaluation(world, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 20.0f + 40.0f / 3), "完了したClipを次の混合から除く");
		ReleaseTimelinePlayer(world, actor, player, systems);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 3) && !IsTimelineControlled(world, actor, "Transform"),
			"重複Clipを停止しても元値と制御権を正しく解放する");
		bool cleaned = false, failed = false;
		BeginBuiltinTimelineEvaluation(world);
		DeferBuiltinTimelineExit(world, [] { throw std::runtime_error("cleanup test"); });
		DeferBuiltinTimelineExit(world, [&] { cleaned = true; });
		try { FlushBuiltinTimelineEvaluation(world, systems); } catch (const std::exception&) { failed = true; }
		Require(failed && cleaned, "一つの終了処理が失敗しても残りを解放する");
	}
}

bool NEMTests::TestTimelineBuiltins() {

	try {
		RegisterBuiltinAnimationProperties();
		CheckPropertyMixing();
		CheckCollisionWindows();
		CheckTrailEmission();
		CheckBeamEmission();
		CheckHitStop();
		CheckDestroyedHitStopOwner();
		CheckMotionAndCamera();
		CheckParticleLeases();
		Require(CheckParticleSimulationReplay(), "粒子とTrailの固定刻みシークを再現する");
		CheckAudioOffsets();
		CheckPrefabFailure();
		CheckPlaybackBoundaries();
		CheckNestedPlaybackMixing();
		std::cout << "TimelineBuiltins: blending / restoration / collision / hitstop / audio offsets passed\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "TimelineBuiltins: " << error.what() << '\n';
		return false;
	}
}
