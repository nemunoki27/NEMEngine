#include "AudioDecoder.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <fstream>
#include <cstring>
// windows
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {

	struct ChunkHeader {
		char id[4];
		int32_t size;
	};

	struct RiffHeader {
		ChunkHeader chunk;
		char type[4];
	};
}

void AudioDecoder::Init() {

	HRESULT hr = MFStartup(MF_VERSION);
	if (SUCCEEDED(hr)) {
		mfStarted_ = true;
	} else {
		Assert::Call(false, "Media Foundationの開始に失敗しました");
	}
}

void AudioDecoder::Finalize() {

	if (mfStarted_) {
		MFShutdown();
		mfStarted_ = false;
	}
}

AudioSoundData AudioDecoder::LoadWaveFile(const std::filesystem::path& filename) {

	std::ifstream file(filename, std::ios::binary);
	Assert::Call(file.is_open(), "WAVファイルを開けません: " + Algorithm::PathToUTF8(filename));

	RiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));

	Assert::Call(std::strncmp(riff.chunk.id, "RIFF", 4) == 0, "WAVファイルのRIFFヘッダーが不正です");
	Assert::Call(std::strncmp(riff.type, "WAVE", 4) == 0, "WAVファイルの形式識別子が不正です");

	ChunkHeader ch{};
	bool fmtFound = false;
	bool dataFound = false;

	std::vector<uint8_t> fmtBlob;
	std::vector<uint8_t> pcm;

	while (file.read(reinterpret_cast<char*>(&ch), sizeof(ch))) {
		if (std::strncmp(ch.id, "fmt ", 4) == 0) {
			Assert::Call(16 <= ch.size, "WAVファイルのfmtチャンクが短すぎます");

			fmtBlob.resize(static_cast<size_t>(ch.size));
			file.read(reinterpret_cast<char*>(fmtBlob.data()), ch.size);
			fmtFound = true;
		} else if (std::strncmp(ch.id, "data", 4) == 0) {
			pcm.resize(static_cast<size_t>(ch.size));
			file.read(reinterpret_cast<char*>(pcm.data()), ch.size);
			dataFound = true;
		} else {
			// それ以外はスキップ
			file.seekg(ch.size, std::ios_base::cur);
		}

		if (fmtFound && dataFound) break;
	}

	file.close();
	Assert::Call(fmtFound && dataFound, "WAVファイルにfmtまたはdataチャンクがありません");
	Assert::Call(!fmtBlob.empty(), "WAVファイルの音声形式が空です");
	Assert::Call(!pcm.empty(), "WAVファイルのPCMデータが空です");

	AudioSoundData sd{};
	sd.formatBlob = std::move(fmtBlob);
	sd.pcmBuffer = std::move(pcm);
	return sd;
}

AudioSoundData AudioDecoder::LoadMP3File(const std::filesystem::path& filename) {

	Assert::Call(mfStarted_, "Audio::InitでMedia Foundationを開始してください");

	const std::wstring wpath = filename.wstring();
	Assert::Call(!wpath.empty(), "音声ファイルのパスが空です");

	ComPtr<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), nullptr, &reader);
	Assert::Call(SUCCEEDED(hr), "Media Foundationの音声リーダー作成に失敗しました");

	// 出力をPCMに指定
	ComPtr<IMFMediaType> outType;
	hr = MFCreateMediaType(&outType);
	Assert::Call(SUCCEEDED(hr), "Media Foundationの音声形式作成に失敗しました");

	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	Assert::Call(SUCCEEDED(hr), "Media Foundationへ音声の主形式を設定できませんでした");

	hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	Assert::Call(SUCCEEDED(hr), "Media FoundationへPCM形式を設定できませんでした");

	hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	Assert::Call(SUCCEEDED(hr), "Media Foundationへ量子化ビット数を設定できませんでした");

	constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

	// SetCurrentMediaType
	hr = reader->SetCurrentMediaType(kAudioStream, nullptr, outType.Get());
	Assert::Call(SUCCEEDED(hr), "Media Foundationへ出力音声形式を設定できませんでした");

	// GetCurrentMediaType
	ComPtr<IMFMediaType> currentType;
	hr = reader->GetCurrentMediaType(kAudioStream, &currentType);
	Assert::Call(SUCCEEDED(hr), "Media Foundationから音声形式を取得できませんでした");

	WAVEFORMATEX* wfx = nullptr;
	UINT32 wfxSize = 0;
	hr = MFCreateWaveFormatExFromMFMediaType(currentType.Get(), &wfx, &wfxSize, MFWaveFormatExConvertFlag_Normal);
	Assert::Call(SUCCEEDED(hr), "Media Foundationの音声形式変換に失敗しました");
	Assert::Call(wfx != nullptr && 0 < wfxSize, "変換後のWAVEFORMATEXが不正です");

	std::vector<uint8_t> fmtBlob;
	fmtBlob.resize(static_cast<size_t>(wfxSize));
	std::memcpy(fmtBlob.data(), wfx, wfxSize);
	CoTaskMemFree(wfx);

	// サンプルを最後まで読む
	std::vector<uint8_t> pcm;
	for (;;) {

		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		ComPtr<IMFSample> sample;

			hr = reader->ReadSample(
			kAudioStream,
			0,
			&streamIndex,
			&flags,
			&timestamp,
				&sample
			);
		Assert::Call(SUCCEEDED(hr), "Media Foundationから音声サンプルを取得できませんでした");
		if (flags & static_cast<DWORD>(MF_SOURCE_READERF_ENDOFSTREAM)) {
			break;
		}

		if (!sample) continue;

		ComPtr<IMFMediaBuffer> buffer;
		hr = sample->ConvertToContiguousBuffer(&buffer);
		Assert::Call(SUCCEEDED(hr), "音声サンプルを連続バッファへ変換できませんでした");

		BYTE* data = nullptr;
		DWORD maxLen = 0;
		DWORD curLen = 0;
		hr = buffer->Lock(&data, &maxLen, &curLen);
		Assert::Call(SUCCEEDED(hr), "Media Foundationの音声バッファをロックできませんでした");

		const size_t oldSize = pcm.size();
		pcm.resize(oldSize + curLen);
		std::memcpy(pcm.data() + oldSize, data, curLen);

		buffer->Unlock();
	}

	Assert::Call(!fmtBlob.empty(), "Media Foundationから取得した音声形式が空です");
	Assert::Call(!pcm.empty(), "Media Foundationから取得したPCMデータが空です");

	AudioSoundData sd{};
	sd.formatBlob = std::move(fmtBlob);
	sd.pcmBuffer = std::move(pcm);
	return sd;
}
