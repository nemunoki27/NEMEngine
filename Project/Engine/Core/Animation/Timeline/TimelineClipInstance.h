#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Core/Animation/Timeline/TimelineControl.h>

// c++
#include <memory>
#include <span>
#include <functional>

namespace Engine {

	class ECSWorld;
	struct SystemContext;
	struct TimelinePlayerSettings;
	struct TimelinePlayerRuntime;
	struct TimelinePlaybackState;
	enum class TimelineEvaluationMode : int32_t { Playback, Seek, Preview };
	enum class TimelineExitReason : int32_t { Completed, Stopped, Reset, Seek, BindingLost, WorldExit, Reload, Faulted };
	struct TimelineResolvedBinding {

		UUID id{};
		std::string name;
		Entity entity = Entity::Null();
	};
	struct TimelineEvaluationContext {

		ECSWorld& world;
		SystemContext& systems;
		Entity owner = Entity::Null();
		Entity target = Entity::Null();
		const TimelineAsset& asset;
		const TimelineTrack& track;
		const TimelineClip& clip;
		std::span<const TimelineResolvedBinding> bindings;
		TimelineControlOwner control{};
		TimelineEvaluationMode mode = TimelineEvaluationMode::Playback;
		double time = 0.0;
		double localTime = 0.0;
		double deltaTime = 0.0;
		double duration = 0.0;
		float normalizedTime = 0.0f;
		float weight = 1.0f;
		bool paused = false;
		const TimelinePlayerSettings* playerSettings = nullptr;
		float playbackRate = 1.0f;
		TimelinePlayerRuntime* output = nullptr;
		bool isScrubbing = false;
		uint64_t samplingRevision = 0;
		std::function<bool(double, Entity, Matrix4x4&)> sampleTargetTransform;
		TimelinePlaybackState* playbackState = nullptr;

		// 追加の参照先を割り当て名か永続識別子から解決する
		Entity ResolveBinding(UUID id) const;
		Entity ResolveBinding(std::string_view name) const;
		const TimelineParameterValue* GetParameter(std::string_view name) const;
	};

	// 設定の解釈は生成時に済ませ、更新中は型付きデータを参照する
	class ITimelineClipInstance {
	public:

		virtual ~ITimelineClipInstance() = default;
		virtual bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) = 0;
		virtual void Evaluate(TimelineEvaluationContext& context) = 0;
		virtual void Retire([[maybe_unused]] TimelineEvaluationContext& context) {}
		virtual void Exit(TimelineEvaluationContext& context, TimelineExitReason reason) = 0;
		virtual bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			[[maybe_unused]] const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) { return false; }
		virtual float GetPreparationProgress() const { return 1.0f; }
		virtual std::span<const Vector3> GetDebugPath() const { return {}; }
	};

	std::unique_ptr<ITimelineClipInstance> CreateBuiltinTimelineClip(const TimelineTrack& track,
		const TimelineClip& clip, std::string& diagnostic);
}
