#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlayerAccess.h"
#include "TimelineBuiltinEvaluation.h"
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using namespace Engine;

namespace {

	// 子Timelineへ親の割り当てとパラメーターを引き継ぐ
	class SubTimelineClip final : public ITimelineClipInstance {
	public:

		explicit SubTimelineClip(const TimelineClip& clip) : assetID_(ParseAssetID(clip.data, "timeline")),
			mapping_(clip.data.value("bindings", nlohmann::json::object())), loop_(clip.data.value("loop", false)) {}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			if (!context.systems.assetDatabase) { diagnostic = "アセットデータベースがありません"; return false; }
			asset_ = GetTimelineAsset(context.world, *context.systems.assetDatabase, assetID_, diagnostic);
			if (!asset_) return false;
			player_.owner = context.control.player;
			player_.inheritedOrder = context.control.order;
			player_.settings.timeline = assetID_;
			player_.settings.priority = context.control.priority;
			player_.settings.loop = loop_;
			player_.settings.timeSource.mode = TimeSourceMode::Unscaled;
			player_.settings.parameters.clear();
			cycle_ = 0;
			clipIn_ = context.clip.clipIn;
			prerolled_ = clipIn_ <= 0;
			SyncBindings(context);
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			if (!asset_) return;
			std::string diagnostic;
			const auto source = GetTimelineAsset(context.world, *context.systems.assetDatabase, assetID_, diagnostic);
			if (!source) throw std::invalid_argument(diagnostic);
			if (asset_ != source) { asset_ = source; SyncBindings(context); }
			for (size_t i = 0; i < bindings_.size(); ++i) {
				Entity target = context.ResolveBinding(bindings_[i].source);
				if (bindings_[i].actor && !target.IsValid()) target = context.target.IsValid() ? context.target : context.owner;
				player_.settings.bindings[i].runtimeTargetID = context.world.GetUUID(target);
			}
			player_.settings.speed = context.playbackRate;
			player_.runtime.paused = context.paused;
			for (const auto& parameter : asset_->parameters) {
				const auto* value = context.GetParameter(parameter.name);
				if (value && value->index() == parameter.value.index())
					SetTimelineParameterValue(player_.settings, parameter.id, *value);
			}
			if (!prerolled_) {
				if (!SampleSource(context, clipIn_, true)) return;
				prerolled_ = true;
			}
			SampleSource(context, (std::max)(0.0, context.localTime), false);
		}

		void Exit(TimelineEvaluationContext& context, TimelineExitReason reason) override {

			ReleaseTimelinePlayer(context.world, context.owner, player_, context.systems, reason);
		}

		void Retire(TimelineEvaluationContext& context) override {

			const auto state = player_.runtime.playback;
			if (!state) return;
			for (auto& clip : state->clips) {
				if (!clip.entered || !clip.instance) continue;
				auto evaluation = MakeTimelineEvaluationContext(context.world, context.owner, *state, clip,
					context.systems, state->evaluatedTime, 0.0);
				clip.instance->Retire(evaluation);
				RetireBuiltinTimelineSamples(context.world, clip.control);
			}
		}

		float GetPreparationProgress() const override {

			return player_.runtime.preparationProgress;
		}

		bool RefreshConfiguration(TimelineEvaluationContext& context, const TimelineClip& clip,
			[[maybe_unused]] std::string& diagnostic) override {

			if (ParseAssetID(clip.data, "timeline") != assetID_) return false;
			mapping_ = clip.data.value("bindings", nlohmann::json::object());
			loop_ = clip.data.value("loop", false);
			player_.settings.loop = loop_;
			SyncBindings(context);
			return true;
		}

	private:

		struct BindingSource {

			Engine::UUID source{};
			bool actor = false;
		};

		AssetID assetID_{};
		nlohmann::json mapping_;
		std::vector<BindingSource> bindings_;
		bool loop_ = false, prerolled_ = false;
		uint64_t cycle_ = 0;
		double clipIn_ = 0;
		std::shared_ptr<const TimelineAsset> asset_;
		TimelinePlayerRecord player_;

		// 子の編集で追加された割り当ても現在の親へ接続する
		void SyncBindings(const TimelineEvaluationContext& context) {

			bindings_.clear();
			player_.settings.bindings.clear();
			for (const auto& slot : asset_->bindings) {
				const std::string source = mapping_.value(slot.name, slot.name);
				const auto sourceID = TryParseUUID16Hex(source).value_or(FindTimelineBinding(context.asset, source));
				bindings_.push_back({sourceID, slot.name == "Actor"});
				player_.settings.bindings.push_back({slot.id});
			}
		}

		// 初期シークも周回再生と同じ準備経路で評価する
		bool SampleSource(TimelineEvaluationContext& context, double sourceTime, bool silent) {

			const double requestedCycle = loop_ ? std::floor(sourceTime / asset_->duration) : 0.0;
			if (requestedCycle > static_cast<double>(cycle_) + 1024.0)
				throw std::invalid_argument("子Timelineの周回数が上限を超えています");
			const auto targetCycle = static_cast<uint64_t>(requestedCycle);
			if (context.isScrubbing) {
				cycle_ = targetCycle;
				Sample(context, loop_ ? std::fmod(sourceTime, asset_->duration) : (std::min)(sourceTime, asset_->duration), !silent, silent);
				return !player_.runtime.preparing;
			}
			if (targetCycle < cycle_) {
				ReleaseTimelinePlayer(context.world, context.owner, player_, context.systems, TimelineExitReason::Seek);
				cycle_ = 0;
			}
			if (targetCycle - cycle_ > 1024) throw std::invalid_argument("子Timelineの周回数が上限を超えています");
			while (cycle_ < targetCycle) {
				Sample(context, asset_->duration, false, silent);
				if (player_.runtime.preparing) return false;
				ReleaseTimelinePlayer(context.world, context.owner, player_, context.systems, TimelineExitReason::Completed);
				++cycle_;
			}
			Sample(context, loop_ ? std::fmod(sourceTime, asset_->duration) : (std::min)(sourceTime, asset_->duration), !silent, silent);
			return !player_.runtime.preparing;
		}

		// 子の通知と受付状態も親Playerから取得できるようにする
		void Sample(TimelineEvaluationContext& context, double time, bool includeWindows, bool silent) {

			player_.runtime.diagnostic.clear();
			const auto mode = silent && context.mode == TimelineEvaluationMode::Playback ? TimelineEvaluationMode::Seek : context.mode;
			if (!player_.runtime.playback && !PrepareTimelinePlayback(context.world, context.owner, player_, *asset_, context.systems, mode))
				throw std::runtime_error(asset_->name + ": " + player_.runtime.diagnostic);
			auto& state = *player_.runtime.playback;
			state.samplingParent = context.output ? context.output->playback : nullptr;
			state.sampleParentTransform = context.sampleTargetTransform;
			state.samplingParentStart = context.clip.start;
			state.samplingParentEnd = context.clip.start + context.clip.duration;
			state.samplingClipIn = context.clip.clipIn;
			state.samplingSpeed = context.clip.speed;
			state.samplingCycle = cycle_ * asset_->duration;
			if (context.isScrubbing) if (const auto parent = state.samplingParent.lock()) {
				for (const auto& binding : state.bindings) {
					const bool transforms = GetTimelineTransformChannels(context.world, state, binding.entity, context.systems) &&
						GetTimelineTransformChannels(context.world, *parent, binding.entity, context.systems);
					const auto skinned = [&](const TimelinePlaybackState& playback) {
						return std::any_of(playback.clips.begin(), playback.clips.end(), [&](const auto& clip) {
							return clip.target == binding.entity && clip.track->kind == TimelineTrackKind::SkinnedAnimation;
						});
					};
					if (transforms || (skinned(state) && skinned(*parent)))
						player_.runtime.diagnostic = "親子Timelineが同じ対象の移動か骨格を重ねる場合、Particleの過去経路は子の結果を優先します";
				}
			}
			if (!EvaluateTimelineAt(context.world, context.owner, player_, *asset_, time, context.systems,
				mode, !silent && mode == TimelineEvaluationMode::Playback && !context.isScrubbing, context.isScrubbing))
				throw std::runtime_error(asset_->name + ": " + player_.runtime.diagnostic);
			if (context.output && !player_.runtime.diagnostic.empty())
				context.output->diagnostic = asset_->name + ": " + player_.runtime.diagnostic;
			if (silent) {
				player_.runtime.notifications.clear();
				player_.runtime.pendingNotifications.clear();
				return;
			}
			if (!context.output) { player_.runtime.notifications.clear(); return; }
			for (auto& notification : player_.runtime.notifications) {
				if (context.output->pendingNotifications.size() >= 4096) {
					context.output->diagnostic = asset_->name + ": 子Timelineの通知が上限へ達しました";
					break;
				}
				if (context.clip.speed > 0.0) notification.time = context.clip.start +
					(notification.time + cycle_ * asset_->duration - context.clip.clipIn) / context.clip.speed;
				else notification.time = context.clip.start;
				if (notification.time < context.clip.start || notification.time > context.clip.start + context.clip.duration) continue;
				context.output->pendingNotifications.push_back(std::move(notification));
			}
			player_.runtime.notifications.clear();
			if (includeWindows) context.output->windows.insert(player_.runtime.windows.begin(), player_.runtime.windows.end());
		}
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineSubTimelineClip(const TimelineClip& clip) {

	return std::make_unique<SubTimelineClip>(clip);
}
