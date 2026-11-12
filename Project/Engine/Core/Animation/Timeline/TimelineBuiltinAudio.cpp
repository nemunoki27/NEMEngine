#include "TimelineBuiltinInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace {

	class AudioClip final : public ITimelineClipInstance {
	public:

		explicit AudioClip(const TimelineClip& clip) : asset_(ParseAssetID(clip.data, "asset")),
			volume_(clip.data.value("volume", 1.0f)), pitch_(clip.data.value("pitch", 1.0f)),
			spatialBlend_(clip.data.value("spatialBlend", 0.0f)), loop_(clip.data.value("loop", false)) {

			if (!std::isfinite(volume_) || volume_ < 0 || !std::isfinite(pitch_) || pitch_ <= 0 ||
				!std::isfinite(spatialBlend_) || spatialBlend_ < 0 || spatialBlend_ > 1)
				throw std::invalid_argument("音量・再生速度・空間設定が不正です");
		}

		bool Enter(TimelineEvaluationContext& context, std::string& diagnostic) override {

			const auto* component = context.world.TryGetComponentForBinding<AudioSourceComponent>(context.target);
			if (!component || !context.systems.assetDatabase || !Audio::TryGetInstance()) {
				diagnostic = "AudioSourceと音声機能が必要です"; return false;
			}
			const auto path = context.systems.assetDatabase->ResolveFullPath(asset_ ? asset_ : component->clip);
			if (path.empty() || !Audio::TryGetInstance()->EnsureLoaded(path)) {
				diagnostic = "音声アセットを読み込めません"; return false;
			}
			key_ = Algorithm::PathToUTF8(path);
			duration_ = Audio::TryGetInstance()->GetSoundDuration(key_);
			return true;
		}

		void Evaluate(TimelineEvaluationContext& context) override {

			auto* audio = Audio::TryGetInstance();
			const auto* component = context.world.TryGetComponentForBinding<AudioSourceComponent>(context.target);
			if (!audio || !component) return;
			const bool advance = !context.isScrubbing && !context.paused && context.playbackRate > 0 &&
				(context.mode == TimelineEvaluationMode::Playback || (context.mode == TimelineEvaluationMode::Preview && context.deltaTime > 0));
			const double sourceTime = context.clip.clipIn + (context.localTime - context.clip.clipIn) * pitch_;
			if (!loop_ && sourceTime >= duration_) { if (voice_) audio->StopVoice(voice_); voice_ = 0; return; }
			if (!voice_ && advance) {
				voice_ = audio->PlayManagedAt(key_, loop_, sourceTime, 0,
					context.mode == TimelineEvaluationMode::Preview ? AudioPlaybackOwner::EditorPreview : AudioPlaybackOwner::Game,
					pitch_ * context.playbackRate);
			}
			if (!voice_) return;
			const double desired = loop_ && duration_ > 0 ? std::fmod(sourceTime, duration_) : sourceTime;
			double drift = std::abs(audio->GetVoicePlaybackTime(voice_) - desired);
			if (loop_ && duration_ > 0) drift = (std::min)(drift, std::abs(duration_ - drift));
			if (context.mode == TimelineEvaluationMode::Seek || sourceTime + 0.001 < previousTime_ ||
				drift > 0.12) audio->SeekVoice(voice_, sourceTime);
			if (advance) audio->ResumeVoice(voice_); else audio->PauseVoice(voice_);
			audio->SetVoiceVolume(voice_, std::clamp(component->volume * volume_ * context.weight, 0.0f, 1.0f));
			audio->SetVoicePitch(voice_, pitch_ * context.playbackRate);
			ResolvedWorldTransform pose;
			if (spatialBlend_ > 0 && TransformWorldUtility::ResolveWorldTransform(context.world, context.target, pose, true)) {
				audio->SetVoiceSpatial(voice_, {pose.matrix.GetTranslationValue(), spatialBlend_,
					component->minDistance, component->maxDistance, component->rolloffMode});
			}
			previousTime_ = sourceTime;
		}

		void Exit([[maybe_unused]] TimelineEvaluationContext& context, [[maybe_unused]] TimelineExitReason reason) override {

			if (auto* audio = Audio::TryGetInstance(); audio && voice_) audio->StopVoice(voice_);
			voice_ = 0;
		}

		bool RefreshConfiguration([[maybe_unused]] TimelineEvaluationContext& context,
			const TimelineClip& clip, [[maybe_unused]] std::string& diagnostic) override {

			AudioClip candidate(clip);
			if (asset_ != candidate.asset_ || loop_ != candidate.loop_) return false;
			volume_ = candidate.volume_;
			pitch_ = candidate.pitch_;
			spatialBlend_ = candidate.spatialBlend_;
			return true;
		}
	private:

		AssetID asset_{};
		std::string key_;
		uint64_t voice_ = 0;
		float volume_ = 1, pitch_ = 1, spatialBlend_ = 0;
		bool loop_ = false;
		double previousTime_ = 0, duration_ = 0;
	};
}

std::unique_ptr<ITimelineClipInstance> Engine::CreateTimelineAudioClip(const TimelineClip& clip) {

	return std::make_unique<AudioClip>(clip);
}
