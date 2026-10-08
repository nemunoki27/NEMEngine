#include "AudioSystem.h"

void Engine::Audio::SetListener(const AudioListenerState& listener) {

	std::lock_guard<std::mutex> lock(mutex_);
	listener_ = listener;
}

void Engine::Audio::SetVoiceSpatial(uint64_t voiceID, const AudioSpatialState& spatial) {

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto& [key, voices] : activeVoices_) {
		for (auto& inst : voices) {
			if (inst.voiceID != voiceID || !inst.voice) { continue; }
			// 2Dへ戻す場合も元の出力行列を復元する
			if (inst.spatial.blend == 0.0f && spatial.blend == 0.0f) { return; }
			inst.spatial = spatial;
			device_.ApplySpatialMatrix(*inst.voice, inst.sound->GetFormat()->nChannels, inst.normalMatrix, spatial, listener_);
			return;
		}
	}
}
