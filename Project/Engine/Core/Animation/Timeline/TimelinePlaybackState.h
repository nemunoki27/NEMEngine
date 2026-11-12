#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelinePlayback.h"
#include "TimelineCollisionGate.h"
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <unordered_map>

namespace Engine {

	struct TimelineClipPlayback {

		const TimelineTrack* track = nullptr;
		const TimelineClip* clip = nullptr;
		std::shared_ptr<ITimelineClipInstance> instance;
		TimelineControlOwner control{};
		Entity target = Entity::Null();
		std::string requiredComponent;
		TimelineNotification notification;
		bool entered = false;
		bool fired = false;
		bool pendingEvent = false;
		bool configurationChanged = false;
	};
	struct TimelinePlaybackState {

		std::shared_ptr<const TimelineAsset> asset;
		const TimelineAsset* source = nullptr;
		std::vector<TimelineClipPlayback> clips;
		std::vector<TimelineResolvedBinding> bindings;
		std::vector<double> boundaries;
		struct PreviewPose {

			Entity entity{};
			Vector3 position{}, scale{};
			Quaternion rotation{};
		};
		std::vector<PreviewPose> previewPoses;
		struct PropertyBase {

			Entity entity{};
			AnimationPropertyDescriptor descriptor;
			AnimationPropertyValue value{};
			bool present = true;
		};
		std::vector<PropertyBase> propertyBases;
		std::weak_ptr<TimelinePlaybackState> samplingParent;
		std::function<bool(double, Entity, Matrix4x4&)> sampleParentTransform;
		double samplingParentStart = 0, samplingParentEnd = 0, samplingClipIn = 0, samplingSpeed = 1, samplingCycle = 0;
		bool root = true;
		std::vector<TimelineCollisionGate> collisionGates;
		std::unordered_map<UUID, bool> groups;
		std::unordered_map<UUID, UUID> branches;
		SystemContext lastContext;
		uint64_t order = 0;
		double delayRemaining = 0.0;
		double evaluatedTime = 0.0;
		bool evaluated = false;
		bool paused = false;
		bool releasing = false;
		bool refreshed = false;
		bool isScrubbing = false;
		uint64_t samplingRevision = 0;
		const TimelinePlayerSettings* settings = nullptr;
		TimelineEvaluationMode mode = TimelineEvaluationMode::Playback;
	};

	// 構造変更が完了してから復元処理を実行する
	void QueueTimelineCleanup(ECSWorld& world, Entity owner, TimelinePlayerRecord&& player);
	void FlushTimelineCleanup(ECSWorld& world);
	// クリップの時間と割り当てを共通の評価情報へまとめる
	TimelineEvaluationContext MakeTimelineEvaluationContext(ECSWorld& world, Entity owner,
		TimelinePlaybackState& state, TimelineClipPlayback& playback, SystemContext& context,
		double time, double deltaTime, const TimelineAsset* definition = nullptr);
	// 置き換えた定義を参照せず終了処理を完了する
	void ExitTimelineClip(ECSWorld& world, Entity owner, const std::shared_ptr<TimelinePlaybackState>& state,
		TimelineClipPlayback& clip, SystemContext& context, double time, TimelineExitReason reason);
	// 同じ識別子の再生状態を保持して編集済み定義へ差し替える
	bool RefreshTimelinePlayback(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		const TimelineAsset& asset, SystemContext& context);
	// 通知と標準クリップの設定を同じ経路で検証する
	bool InitializeTimelineClipPlayback(const TimelineTrack& track, const TimelineClip& clip,
		TimelineClipPlayback& playback, std::string& diagnostic);
	void RebuildTimelineBoundaries(TimelinePlaybackState& state);
	// プレビュー参照先を変更する前の姿勢を保持する
	void CaptureTimelinePreviewPoses(ECSWorld& world, TimelinePlaybackState& state);
	// 履歴再生用の姿勢だけを通知やコンポーネント変更なしで取得する
	bool SampleTimelineTargetTransform(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		TimelinePlaybackState& state, SystemContext& context, double time, Entity target, Matrix4x4& result);
	uint64_t GetTimelineSamplingRevision(ECSWorld& world, const TimelinePlaybackState& state, SystemContext& context);
	uint8_t GetTimelineTransformChannels(ECSWorld& world, const TimelinePlaybackState& state, Entity target, SystemContext& context);
	// 親グループと条件分岐を再生単位で解決する
	bool IsTimelineTrackActive(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		TimelinePlaybackState& state, const TimelineTrack& track, double time);
	// 管理対象のアセットと実行時クリップを準備する
	bool PrepareTimelinePlayback(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		const TimelineAsset& asset, SystemContext& context, TimelineEvaluationMode mode);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineCustomClip(const TimelineTrack& track,
		const TimelineClip& clip, std::string& diagnostic);
	std::unique_ptr<ITimelineClipInstance> CreateTimelineSubTimelineClip(const TimelineClip& clip);
	std::unique_ptr<ITimelineClipInstance> MakeTimelineClipInstance(const TimelineTrack& track,
		const TimelineClip& clip, std::string& diagnostic);
}
