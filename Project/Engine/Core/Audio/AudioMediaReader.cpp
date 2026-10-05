#include "AudioMediaReader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstring>
#include <memory>
// windows
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace {

	struct AudioFormatDeleter {
		void operator()(WAVEFORMATEX* format) const { CoTaskMemFree(format); }
	};

	// 例外時もMedia BufferのLockを解除する
	struct AudioMediaBufferLock {
		IMFMediaBuffer& buffer;
		bool locked = false;
		~AudioMediaBufferLock() { if (locked) { buffer.Unlock(); } }
	};
}

std::optional<Engine::AudioSoundData> Engine::ReadAudioMedia(const std::filesystem::path& path, std::string& error) {

	error.clear();
	auto failed = [&](HRESULT result, const char* message) {
		if (SUCCEEDED(result)) { return false; }
		error = std::string(message) + " HRESULT=" + std::to_string(result);
		return true;
	};
	ComPtr<IMFSourceReader> reader;
	if (failed(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "音声リーダーを作成できません")) {
		return std::nullopt;
	}
	// PCM16bitへ統一してから全サンプルを読む
	ComPtr<IMFMediaType> output;
	if (failed(MFCreateMediaType(&output), "出力形式を作成できません") ||
		failed(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "音声形式を設定できません") ||
		failed(output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "PCM形式を設定できません") ||
		failed(output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "量子化ビット数を設定できません")) {
		return std::nullopt;
	}
	constexpr DWORD kStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
	if (failed(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE), "対象Streamを選択できません") ||
		failed(reader->SetStreamSelection(kStream, TRUE), "音声Streamを選択できません") ||
		failed(reader->SetCurrentMediaType(kStream, nullptr, output.Get()), "PCMへ変換できません")) {
		return std::nullopt;
	}
	ComPtr<IMFMediaType> current;
	if (failed(reader->GetCurrentMediaType(kStream, &current), "音声形式を取得できません")) return std::nullopt;
	WAVEFORMATEX* rawFormat = nullptr;
	UINT32 formatSize = 0;
	HRESULT result = MFCreateWaveFormatExFromMFMediaType(current.Get(), &rawFormat, &formatSize, MFWaveFormatExConvertFlag_Normal);
	std::unique_ptr<WAVEFORMATEX, AudioFormatDeleter> format(rawFormat);
	if (failed(result, "音声形式を変換できません") || !format || formatSize < sizeof(WAVEFORMATEX)) {
		if (error.empty()) { error = "変換後の音声形式が不正です"; }
		return std::nullopt;
	}
	AudioSoundData sound;
	sound.formatBlob.resize(formatSize);
	std::memcpy(sound.formatBlob.data(), format.get(), formatSize);
	for (;;) {
		DWORD flags = 0;
		ComPtr<IMFSample> sample;
		if (failed(reader->ReadSample(kStream, 0, nullptr, &flags, nullptr, &sample), "音声サンプルを取得できません")) {
			return std::nullopt;
		}
		if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) {
			error = "音声の途中で形式が変化したか、復号に失敗しました";
			return std::nullopt;
		}
		if (sample) {
			ComPtr<IMFMediaBuffer> buffer;
			if (failed(sample->ConvertToContiguousBuffer(&buffer), "音声Bufferを取得できません")) return std::nullopt;
			AudioMediaBufferLock lock{ *buffer.Get() };
			BYTE* bytes = nullptr;
			DWORD length = 0;
			if (failed(buffer->Lock(&bytes, nullptr, &length), "音声BufferをLockできません")) return std::nullopt;
			lock.locked = true;
			if (length > XAUDIO2_MAX_BUFFER_BYTES - sound.pcmBuffer.size()) {
				error = "復号したPCMが最大サイズを超えました";
				return std::nullopt;
			}
			if (length > 0) { sound.pcmBuffer.insert(sound.pcmBuffer.end(), bytes, bytes + length); }
		}
		// 終端frameに含まれるサンプルも取り込む
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
	}
	if (sound.pcmBuffer.empty() || format->nBlockAlign == 0 || sound.pcmBuffer.size() % format->nBlockAlign != 0) {
		error = "復号したPCMが空か、転送単位と一致しません";
		return std::nullopt;
	}
	return sound;
}
