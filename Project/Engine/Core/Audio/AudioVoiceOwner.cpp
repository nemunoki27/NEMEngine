#include "AudioVoiceOwner.h"

void Engine::AudioVoiceDeleter::operator()(IXAudio2SourceVoice* voice) const {

	// PCMの参照が解放される前にVoiceを破棄する
	voice->DestroyVoice();
}
