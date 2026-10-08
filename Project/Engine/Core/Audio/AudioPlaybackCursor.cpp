#include "AudioPlaybackCursor.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

uint32_t Engine::AudioPlaybackCursor::Resolve(uint64_t clock, uint32_t sampleCount) const {

	if (sampleCount == 0) { return 0; }
	// EOSでXAudio2の時計が戻った場合は新しい時計を使う
	uint64_t elapsed = (clock >= clockOrigin ? clock - clockOrigin : clock) % sampleCount;
	uint64_t origin = sampleOrigin % sampleCount;
	return static_cast<uint32_t>(reverse ? (origin + sampleCount - elapsed) % sampleCount : (origin + elapsed) % sampleCount);
}

std::vector<uint8_t> Engine::ReverseAudioPCM(const AudioSoundData& sound) {

	uint32_t stride = sound.GetFormat()->nBlockAlign;
	if (stride == 0 || sound.pcmBuffer.size() % stride != 0) { return {}; }
	std::vector<uint8_t> reversed(sound.pcmBuffer.size());
	// 左右チャンネルの並びを崩さずサンプル単位で反転する
	for (size_t offset = 0; offset < reversed.size(); offset += stride) {
		std::copy_n(sound.pcmBuffer.data() + sound.pcmBuffer.size() - stride - offset, stride, reversed.data() + offset);
	}
	return reversed;
}
