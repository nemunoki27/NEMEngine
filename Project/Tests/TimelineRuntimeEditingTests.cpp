#include "TimelineRuntimeEditingTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/Animation/Timeline/TimelinePlaybackState.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Animation/MotionAnimationComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Engine;

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	bool Near(float a, float b) {

		return std::abs(a - b) < 0.0002f;
	}

	TimelineAsset MakeAsset() {

		auto asset = CreateDefaultTimelineAsset("LiveEditing");
		asset.guid = AssetID::New();
		asset.durationMode = TimelineDurationMode::FixedLength;
		asset.duration = 2.0;
		return asset;
	}

	TimelineTrack MakeTrack(const TimelineAsset& asset, TimelineTrackKind kind, double duration = 2.0) {

		TimelineTrack track;
		track.id = Engine::UUID::New();
		track.name = std::string(GetTimelineTrackKindName(kind));
		track.kind = kind;
		track.binding = asset.bindings.front().id;
		TimelineClip clip;
		clip.id = Engine::UUID::New();
		clip.duration = duration;
		clip.data = CreateDefaultTimelineClipData(kind);
		track.clips.push_back(std::move(clip));
		return track;
	}

	SystemContext MakeContext() {

		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = context.unscaledDeltaTime = 0.01f;
		return context;
	}

	// 旧形式を固定長として読み新規アセットだけ末尾へ追従させる
	void CheckDurationAndSource() {

		auto asset = MakeAsset();
		auto track = MakeTrack(asset, TimelineTrackKind::Property);
		track.clips.front().sourceDuration = 4.0;
		asset.tracks.push_back(track);
		auto saved = nlohmann::json(asset);
		saved.erase("durationMode");
		saved["tracks"][0]["clips"][0].erase("sourceDuration");
		const auto old = saved.get<TimelineAsset>();
		Require(old.durationMode == TimelineDurationMode::FixedLength && old.duration == 2.0,
			"旧形式の固定長を保持する");
		Require(GetTimelineClipSourceDuration(old.tracks.front().clips.front()) == 2.0, "旧素材の評価長を補完する");
		const auto restored = nlohmann::json(asset).get<TimelineAsset>();
		Require(GetTimelineClipSourceDuration(restored.tracks.front().clips.front()) == 4.0, "素材評価長を保存する");
		asset.durationMode = TimelineDurationMode::BasedOnClips;
		asset.tracks.front().clips.front().start = 1.0;
		UpdateTimelineAssetDuration(asset);
		Require(asset.duration == 3.0, "自動長をクリップ末尾に合わせる");
		Require(CreateDefaultTimelineAsset("New").durationMode == TimelineDurationMode::BasedOnClips,
			"新規アセットの既定は自動長にする");
	}

	// 定義の差し替えで原点や再生時刻や通知済み状態を失わない
	void CheckLiveMotionAndEvents() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.GetComponent<TransformComponent>(actor).localPos = {3, 0, 0};
		auto asset = MakeAsset();
		auto motion = MakeTrack(asset, TimelineTrackKind::Motion);
		MotionAnimationSettings settings;
		settings.offset = {4, 0, 0};
		settings.easing = EasingType::Linear;
		motion.clips.front().data["settings"] = settings;
		auto events = MakeTrack(asset, TimelineTrackKind::Event, 0.0);
		events.clips.front().start = 0.2;
		events.clips.front().data["name"] = "Impact";
		asset.tracks = {motion, events};
		TimelinePlayerRecord player;
		player.runtime.playing = true;
		player.runtime.cycles = 3;
		auto context = MakeContext();
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.5, context, TimelineEvaluationMode::Playback, true),
			"変更前の再生を評価する");
		const auto state = player.runtime.playback;
		const auto* instance = state->clips.front().instance.get();
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 4), "初期原点から移動する");
		Require(player.runtime.notifications.size() == 1, "通過イベントを一度通知する");
		auto edited = asset;
		settings.offset = {8, 0, 0};
		edited.tracks.front().clips.front().data["settings"] = settings;
		edited.tracks.back().clips.front().data["floatParam"] = 2.0f;
		player.runtime.paused = true;
		Require(EvaluateTimelineAt(world, actor, player, edited, 0.5, context, TimelineEvaluationMode::Playback, true),
			"同時刻に変更を反映する");
		Require(player.runtime.playback == state && state->clips.front().instance.get() == instance,
			"同じクリップの個体を作り直さない");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 5), "変更後も元の原点を使う");
		Require(player.runtime.time == 0.5 && player.runtime.cycles == 3 && player.runtime.paused,
			"時刻と周回数と一時停止を維持する");
		Require(player.runtime.notifications.size() == 1, "編集で通知を再送しない");
		auto future = edited;
		auto marker = future.tracks.back().clips.front();
		marker.id = Engine::UUID::New();
		marker.start = 0.8;
		future.tracks.back().clips.push_back(marker);
		Require(EvaluateTimelineAt(world, actor, player, future, 0.9, context, TimelineEvaluationMode::Playback, true),
			"編集後の前方評価を続ける");
		Require(player.runtime.notifications.size() == 2, "追加した将来の通知は一度だけ送る");
		ReleaseTimelinePlayer(world, actor, player, context);
		Require(!IsTimelineControlled(world, actor, "Transform"), "停止後に制御を残さない");
	}

	// トリミング後も元の素材時刻で補間する
	void CheckTrimmedProperty() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		auto asset = MakeAsset();
		auto track = MakeTrack(asset, TimelineTrackKind::Property);
		auto& clip = track.clips.front();
		clip.sourceDuration = 2.0;
		clip.start = 0.5;
		clip.duration = 1.5;
		clip.clipIn = 0.5;
		clip.data["to"] = Vector3(10, 0, 0).ToJson();
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		auto context = MakeContext();
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.0, context), "切り取った区間を評価する");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 5), "配置長で素材を伸縮しない");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 保存しない素材更新を同じ個体へ反映して元の値へ戻す
	void CheckAnimationDraft() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.GetComponent<TransformComponent>(actor).localPos = {2, 3, 4};
		const auto sourceID = AssetID::New();
		auto source = std::make_shared<AnimationClipAsset>();
		source->guid = sourceID;
		source->duration = 2;
		AnimationCurveTrack property;
		property.binding = {"Transform", "localPos", AnimationValueType::Vector3};
		property.channels = MakeDefaultAnimationChannels(AnimationValueType::Vector3);
		property.channels.front().AddKey(0, 0);
		property.channels.front().AddKey(2, 10);
		source->curveTracks.push_back(property);
		Require(PublishAnimationClipAssetOverride(world, sourceID, source), "保存前の素材を公開する");
		auto asset = MakeAsset();
		auto track = MakeTrack(asset, TimelineTrackKind::Property);
		track.clips.front().data["asset"] = ToAssetReferenceJson(sourceID);
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		auto context = MakeContext();
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.0, context), "保存前の素材を再生する");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 5), "保存前のキーを使用する");
		const auto* instance = player.runtime.playback->clips.front().instance.get();
		auto edited = std::make_shared<AnimationClipAsset>(*source);
		edited->curveTracks.front().channels.front().keys.back().value = 20;
		Require(PublishAnimationClipAssetOverride(world, sourceID, edited), "変更した素材世代を公開する");
		Require(EvaluateTimelineAt(world, actor, player, asset, 1.0, context), "同時刻の素材更新を反映する");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 10), "変更したキーへ更新する");
		Require(player.runtime.playback->clips.front().instance.get() == instance, "素材更新で再生個体を失わない");
		ReleaseTimelinePlayer(world, actor, player, context);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 2), "初期値を更新後も復元する");
		ClearAnimationClipAssetOverrides(world);
		Require(!GetAnimationClipAssetOverride(world, sourceID), "Worldの素材上書きを解除する");
	}

	// 判定トラックの削除で有効状態と所有権を残さない
	void CheckRemovedCollision() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<CollisionComponent>(actor);
		world.GetComponent<CollisionComponent>(actor).enabled = false;
		auto asset = MakeAsset();
		asset.tracks.push_back(MakeTrack(asset, TimelineTrackKind::Collision));
		TimelinePlayerRecord player;
		auto context = MakeContext();
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.5, context), "判定を有効にする");
		Require(world.GetComponent<CollisionComponent>(actor).enabled, "区間内は判定が有効になる");
		auto edited = asset;
		edited.tracks.clear();
		Require(EvaluateTimelineAt(world, actor, player, edited, 0.5, context), "判定トラック削除を反映する");
		Require(!world.GetComponent<CollisionComponent>(actor).enabled && !IsTimelineControlled(world, actor, "Collision"),
			"削除した判定の状態と所有権を残さない");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 同じ区間内の前後移動では個体と相対移動の原点を保持する
	void CheckContinuousScrubbing() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<CollisionComponent>(actor);
		world.GetComponent<TransformComponent>(actor).localPos = {3, 0, 0};
		auto asset = MakeAsset();
		auto motion = MakeTrack(asset, TimelineTrackKind::Motion, 1.0);
		motion.clips.front().start = 0.5;
		MotionAnimationSettings settings;
		settings.offset = {4, 0, 0};
		motion.clips.front().data["settings"] = settings;
		auto collision = MakeTrack(asset, TimelineTrackKind::Collision, 0.5);
		collision.clips.front().start = 0.7;
		auto events = MakeTrack(asset, TimelineTrackKind::Event, 0.0);
		events.clips.front().start = 0.8;
		asset.tracks = {motion, collision, events};
		TimelinePlayerRecord player;
		auto context = MakeContext();
		const auto sample = [&](double time, float expected, bool colliding) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, context, TimelineEvaluationMode::Preview, true, true),
				"時刻を直接スクラブする");
			Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, expected), "前後スクラブの相対座標が一致する");
			Require(world.GetComponent<CollisionComponent>(actor).enabled == colliding, "前後スクラブの判定区間が一致する");
			Require(player.runtime.notifications.empty(), "スクラブで通知を送らない");
		};
		sample(1.0, 5, true);
		const auto state = player.runtime.playback;
		const auto* instance = state->clips.front().instance.get();
		sample(0.75, 4, true);
		sample(1.25, 6, false);
		Require(state == player.runtime.playback && state->clips.front().instance.get() == instance,
			"逆スクラブでPlayerと継続中のクリップを作り直さない");
		sample(0.25, 3, false);
		sample(1.0, 5, true);
		sample(1.8, 7, false);
		sample(1.0, 5, true);
		Matrix4x4 historical;
		Require(SampleTimelineTargetTransform(world, actor, player, *state, context, 0.75, actor, historical) &&
			Near(historical.GetTranslationValue().x, 4), "履歴サンプルで元の時刻の相対位置を取得する");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 5), "履歴サンプルで現在のWorldを書き換えない");
		ReleaseTimelinePlayer(world, actor, player, context);
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 3), "スクラブ終了時に元の姿勢を復元する");
	}

	struct ScrubProbe {

		uint32_t entered = 0, exited = 0, sampled = 0;
		bool scrubbing = false;
		double time = 0, delta = -1;
	};
	class ScrubProbeClip final : public ITimelineClipInstance {
	public:

		explicit ScrubProbeClip(ScrubProbe& probe) : probe_(probe) {}
		bool Enter([[maybe_unused]] TimelineEvaluationContext& context, [[maybe_unused]] std::string& diagnostic) override { ++probe_.entered; return true; }
		void Evaluate(TimelineEvaluationContext& context) override {

			++probe_.sampled;
			probe_.scrubbing = context.isScrubbing;
			probe_.time = context.localTime;
			probe_.delta = context.deltaTime;
		}
		void Exit([[maybe_unused]] TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override { ++probe_.exited; }
	private:

		ScrubProbe& probe_;
	};

	// 利用者の状態を持つクリップへ絶対時刻だけを渡す
	void CheckScrubbingContext() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		auto asset = MakeAsset();
		auto track = MakeTrack(asset, TimelineTrackKind::Custom, 1.5);
		track.clips.front().start = 0.2;
		track.clips.front().clipIn = 0.3;
		track.clips.front().speed = 2;
		track.clips.front().data = {{"typeName", "ScrubFixture"}};
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		auto context = MakeContext();
		Require(PrepareTimelinePlayback(world, actor, player, asset, context, TimelineEvaluationMode::Seek), "監視クリップを準備する");
		ScrubProbe probe;
		player.runtime.playback->clips.front().instance = std::make_shared<ScrubProbeClip>(probe);
		for (double time : {0.4, 0.9, 0.3, 0.3, 1.2}) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, context, TimelineEvaluationMode::Seek, true, true), "監視クリップをスクラブする");
			Require(probe.entered == 1 && probe.exited == 0 && probe.scrubbing && probe.delta == 0 &&
				std::abs(probe.time - (0.3 + (time - 0.2) * 2)) < 1e-9, "個体を保持して素材時刻とスクラブフラグを渡す");
		}
		ReleaseTimelinePlayer(world, actor, player, context);
		Require(probe.exited == 1, "継続中クリップの終了を一度だけ呼ぶ");
	}

	// 親の経路と素材変更を現在の描画姿勢へ影響させず参照する
	void CheckHistoricalParentSampling() {

		ECSWorld world;
		const auto actor = world.CreateEntity(), child = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TransformComponent>(child);
		world.AddComponent<HierarchyComponent>(child);
		world.GetComponent<HierarchyComponent>(child).parent = actor;
		world.GetComponent<TransformComponent>(child).localPos = {0, 2, 0};
		auto asset = MakeAsset();
		auto property = MakeTrack(asset, TimelineTrackKind::Property);
		property.clips.front().data["to"] = Vector3(10, 0, 0).ToJson();
		asset.tracks.push_back(property);
		TimelinePlayerRecord player;
		auto context = MakeContext();
		Require(EvaluateTimelineAt(world, actor, player, asset, 1, context, TimelineEvaluationMode::Preview, false, true), "親の現在姿勢を評価する");
		const auto state = player.runtime.playback;
		Matrix4x4 historical;
		Require(SampleTimelineTargetTransform(world, actor, player, *state, context, 0.5, child, historical), "子の過去姿勢を取得する");
		Require(Near(historical.GetTranslationValue().x, 2.5f) && Near(historical.GetTranslationValue().y, 2), "過去の親行列を子へ合成する");
		Require(Near(world.GetComponent<TransformComponent>(actor).localPos.x, 5), "親の過去サンプルで現在値を残す");
		TimelinePlayerRecord nested;
		auto nestedAsset = MakeAsset();
		Require(PrepareTimelinePlayback(world, actor, nested, nestedAsset, context, TimelineEvaluationMode::Preview), "子の履歴サンプルを準備する");
		auto& nestedState = *nested.runtime.playback;
		nestedState.samplingParent = state;
		nestedState.sampleParentTransform = [&](double time, Entity entity, Matrix4x4& result) {
			return SampleTimelineTargetTransform(world, actor, player, *state, context, time, entity, result);
		};
		nestedState.samplingParentStart = 0.5;
		nestedState.samplingParentEnd = 1.5;
		nestedState.samplingClipIn = 0.2;
		nestedState.samplingSpeed = 2.0;
		Require(SampleTimelineTargetTransform(world, actor, nested, nestedState, context, 0.5, child, historical) &&
			Near(historical.GetTranslationValue().x, 3.25f) && Near(historical.GetTranslationValue().y, 2),
			"子の素材時刻を親へ戻して親Timelineの過去経路も取得する");
		ReleaseTimelinePlayer(world, actor, nested, context);
		const auto revision = GetTimelineSamplingRevision(world, *state, context);
		world.GetComponent<TransformComponent>(child).localPos.y = 3;
		asset.bindings.push_back({Engine::UUID::New(), "Emitter", "Transform"});
		auto changed = asset;
		Require(EvaluateTimelineAt(world, actor, player, changed, 1, context, TimelineEvaluationMode::Preview, false, true), "編集後の定義を反映する");
		Require(GetTimelineSamplingRevision(world, *state, context) != revision, "定義変更で過去サンプルの世代を変更する");
		auto blended = changed;
		blended.tracks.front().clips.front().data["from"] = blended.tracks.front().clips.front().data["to"] = Vector3(2, 0, 0).ToJson();
		auto overlay = MakeTrack(blended, TimelineTrackKind::Property);
		overlay.clips.front().data["from"] = overlay.clips.front().data["to"] = Vector3(6, 0, 0).ToJson();
		blended.tracks.push_back(overlay);
		Require(EvaluateTimelineAt(world, actor, player, blended, 1, context, TimelineEvaluationMode::Preview, false, true) &&
			SampleTimelineTargetTransform(world, actor, player, *state, context, 1, actor, historical) &&
			Near(historical.GetTranslationValue().x, 4) && Near(world.GetComponent<TransformComponent>(actor).localPos.x, 4),
			"重ねた値の履歴サンプルも実際の重み付き合成と一致する");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 終点保持の数値とローカル時計も開始前へ戻せる
	void CheckPersistentPropertyScrubbing() {

		ECSWorld world;
		const auto actor = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TimeScaleComponent>(actor);
		world.AddComponent<PerspectiveCameraComponent>(actor);
		world.GetComponent<TimeScaleComponent>(actor).timeScale = 0.5f;
		world.GetComponent<PerspectiveCameraComponent>(actor).fovY = 30;
		auto asset = MakeAsset();
		auto track = MakeTrack(asset, TimelineTrackKind::Property, 0.5);
		track.clips.front().start = 0.5;
		track.clips.front().data = {{"binding", AnimationPropertyBinding{"PerspectiveCamera", "fovY", AnimationValueType::Float}},
			{"from", 30.0f}, {"to", 90.0f}, {"restoreOnEnd", false}};
		asset.tracks.push_back(track);
		TimelinePlayerRecord player;
		auto context = MakeContext();
		for (const auto& [time, value] : {std::pair{1.3, 90.0f}, {0.2, 30.0f}, {0.75, 60.0f}, {0.2, 30.0f}}) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, context, TimelineEvaluationMode::Seek, false, true), "終点保持の数値をスクラブする");
			Require(Near(world.GetComponent<PerspectiveCameraComponent>(actor).fovY, value), "開始前へ戻しても最初の基準値を復元する");
		}
		ReleaseTimelinePlayer(world, actor, player, context);
		asset.tracks.clear();
		auto clock = MakeTrack(asset, TimelineTrackKind::TimeScale, 0.5);
		clock.clips.front().start = 0.5;
		clock.clips.front().data["scale"] = 0.25f;
		asset.tracks.push_back(clock);
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.75, context, TimelineEvaluationMode::Preview, false, true) &&
			Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0.25f), "通常のローカル時間倍率を指定時刻へ反映する");
		Require(EvaluateTimelineAt(world, actor, player, asset, 0.2, context, TimelineEvaluationMode::Preview, false, true) &&
			Near(world.GetComponent<TimeScaleComponent>(actor).timeScale, 0.5f), "時間倍率の区間外では元へ戻す");
		ReleaseTimelinePlayer(world, actor, player, context);
	}

	// 距離分岐は直前の移動を反映したグループ開始位置で判定する
	void CheckDistanceBranchScrubbing() {

		ECSWorld world;
		const auto actor = world.CreateEntity(), target = world.CreateEntity();
		world.AddComponent<TransformComponent>(actor);
		world.AddComponent<TransformComponent>(target);
		world.AddComponent<CollisionComponent>(actor);
		world.GetComponent<TransformComponent>(target).localPos = {5, 0, 0};
		auto asset = MakeAsset();
		const auto targetSlot = Engine::UUID::New();
		asset.bindings.push_back({targetSlot, "Enemy", "Transform"});
		auto motion = MakeTrack(asset, TimelineTrackKind::Motion, 0.5);
		MotionAnimationSettings settings;
		settings.offset = {4, 0, 0};
		motion.clips.front().data["settings"] = settings;
		TimelineTrack group;
		group.id = Engine::UUID::New();
		group.kind = TimelineTrackKind::Group;
		group.start = 0.5;
		group.duration = 1.0;
		TimelineCondition condition;
		condition.kind = TimelineConditionKind::Distance;
		condition.firstBinding = asset.bindings.front().id;
		condition.secondBinding = targetSlot;
		condition.comparison = TimelineComparison::Less;
		condition.distance = 2;
		group.conditions.push_back(condition);
		auto collision = MakeTrack(asset, TimelineTrackKind::Collision, 1.0);
		collision.parent = group.id;
		collision.clips.front().start = 0.5;
		asset.tracks = {motion, group, collision};
		TimelinePlayerRecord player;
		player.settings.bindings.push_back({targetSlot, {}, world.GetUUID(target)});
		auto context = MakeContext();
		for (double time : {1.0, 0.8, 0.2, 1.0}) {
			Require(EvaluateTimelineAt(world, actor, player, asset, time, context, TimelineEvaluationMode::Preview, false, true), "距離分岐を前後にスクラブする");
			Require(world.GetComponent<CollisionComponent>(actor).enabled == (time >= 0.5), "距離条件に前の移動を反映し巻き戻しでも同じ分岐を選ぶ");
		}
		ReleaseTimelinePlayer(world, actor, player, context);
	}
}

bool NEMTests::TestTimelineRuntimeEditing() {

	try {
		RegisterBuiltinAnimationProperties();
		CheckDurationAndSource();
		CheckLiveMotionAndEvents();
		CheckTrimmedProperty();
		CheckAnimationDraft();
		CheckRemovedCollision();
		CheckContinuousScrubbing();
		CheckScrubbingContext();
		CheckHistoricalParentSampling();
		CheckPersistentPropertyScrubbing();
		CheckDistanceBranchScrubbing();
		return true;
	} catch (const std::exception& error) {
		std::cerr << "TimelineRuntimeEditing: " << error.what() << '\n';
		return false;
	}
}
