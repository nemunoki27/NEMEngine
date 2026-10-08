#include "AudioWaveReader.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace {

	struct WaveChunkHeader {

		char id[4];
		uint32_t size;
	};

	struct WaveRiffHeader {

		WaveChunkHeader chunk;
		char type[4];
	};

	// PCMと浮動小数点形式の転送単位を検証する
	bool ValidateWaveFormat(const Engine::AudioSoundData& sound, std::string& error, bool* requiresDecode) {

		WAVEFORMATEX format{};
		std::memcpy(&format, sound.formatBlob.data(), sizeof(format));
		if (sound.formatBlob.size() < sizeof(format) + format.cbSize ||
			format.nChannels == 0 || format.nChannels > XAUDIO2_MAX_AUDIO_CHANNELS ||
			format.nSamplesPerSec < XAUDIO2_MIN_SAMPLE_RATE || format.nSamplesPerSec > XAUDIO2_MAX_SAMPLE_RATE ||
			format.nBlockAlign == 0) {
			error = "WAVの音声形式とPCMサイズが一致しません";
			return false;
		}
		uint16_t encoding = format.wFormatTag;
		if (encoding != WAVE_FORMAT_PCM && encoding != WAVE_FORMAT_IEEE_FLOAT && encoding != WAVE_FORMAT_EXTENSIBLE) {
			// 圧縮形式は境界確認後にMedia Foundationへ渡す
			if (requiresDecode) { *requiresDecode = true; }
			error = "WAVの圧縮音声には復号が必要です";
			return false;
		}
		if (encoding == WAVE_FORMAT_EXTENSIBLE) {

			// 拡張形式のSubtypeと有効ビット数を検証する
			if (format.cbSize < 22 || sound.formatBlob.size() < sizeof(WAVEFORMATEXTENSIBLE)) {
				error = "WAVの拡張形式が短すぎます";
				return false;
			}
			WAVEFORMATEXTENSIBLE extended{};
			std::memcpy(&extended, sound.formatBlob.data(), sizeof(extended));
			constexpr std::array<uint8_t, 8> kSubtypeSuffix{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
			if ((extended.SubFormat.Data1 != WAVE_FORMAT_PCM && extended.SubFormat.Data1 != WAVE_FORMAT_IEEE_FLOAT) ||
				extended.SubFormat.Data2 != 0 || extended.SubFormat.Data3 != 0x0010 ||
				std::memcmp(extended.SubFormat.Data4, kSubtypeSuffix.data(), kSubtypeSuffix.size()) != 0 ||
				extended.Samples.wValidBitsPerSample == 0 || extended.Samples.wValidBitsPerSample > format.wBitsPerSample) {
				error = "WAVの拡張Subtypeが未対応です";
				return false;
			}
			encoding = static_cast<uint16_t>(extended.SubFormat.Data1);
		}
		bool validBits = encoding == WAVE_FORMAT_PCM ?
			format.wBitsPerSample == 8 || format.wBitsPerSample == 16 || format.wBitsPerSample == 24 || format.wBitsPerSample == 32 :
			encoding == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32;
		uint32_t blockSize = static_cast<uint32_t>(format.nChannels) * format.wBitsPerSample / 8;
		if (!validBits || format.nBlockAlign != blockSize || sound.pcmBuffer.size() % format.nBlockAlign != 0 ||
			static_cast<uint64_t>(format.nAvgBytesPerSec) != static_cast<uint64_t>(format.nSamplesPerSec) * blockSize) {
			error = "WAVのPCM形式が未対応か不正です";
			return false;
		}
		return true;
	}
}

std::optional<Engine::AudioSoundData> Engine::ReadAudioWave(const std::filesystem::path& path,
	std::string& error, bool* requiresDecode) {

	error.clear();
	if (requiresDecode) { *requiresDecode = false; }
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file || file.tellg() < static_cast<std::streamoff>(sizeof(WaveRiffHeader))) {
		error = "WAVファイルを開けないか、ヘッダーが短すぎます";
		return std::nullopt;
	}
	uint64_t fileSize = static_cast<uint64_t>(file.tellg());
	file.seekg(0);
	WaveRiffHeader riff{};
	file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
	uint64_t riffEnd = static_cast<uint64_t>(riff.chunk.size) + sizeof(WaveChunkHeader);
	if (std::memcmp(riff.chunk.id, "RIFF", 4) != 0 || std::memcmp(riff.type, "WAVE", 4) != 0 ||
		riffEnd < sizeof(riff) || fileSize < riffEnd) {
		error = "WAVのRIFF識別子またはサイズが不正です";
		return std::nullopt;
	}

	AudioSoundData sound{};
	uint64_t offset = sizeof(riff);
	bool formatFound = false;
	bool dataFound = false;
	while (offset < riffEnd) {

		// サイズを確認してから読込とメモリ確保を行う
		if (riffEnd - offset < sizeof(WaveChunkHeader)) {
			error = "WAVのチャンクヘッダーが途中で切れています";
			return std::nullopt;
		}
		WaveChunkHeader chunk{};
		file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk));
		offset += sizeof(chunk);
		uint64_t paddedSize = static_cast<uint64_t>(chunk.size) + (chunk.size & 1u);
		if (!file || riffEnd - offset < paddedSize) {
			error = "WAVのチャンクがRIFF境界を超えています";
			return std::nullopt;
		}
		if (std::memcmp(chunk.id, "fmt ", 4) == 0) {
			if (formatFound || chunk.size < 16 || chunk.size == 17 || chunk.size > sizeof(WAVEFORMATEX) + 65535u) {
				error = "WAVのfmtチャンクが不正です";
				return std::nullopt;
			}
			sound.formatBlob.resize((std::max)(static_cast<size_t>(chunk.size), sizeof(WAVEFORMATEX)), 0);
			file.read(reinterpret_cast<char*>(sound.formatBlob.data()), chunk.size);
			formatFound = true;
		} else if (std::memcmp(chunk.id, "data", 4) == 0) {
			if (dataFound || chunk.size == 0 || chunk.size > XAUDIO2_MAX_BUFFER_BYTES) {
				error = "WAVのdataチャンクが不正です";
				return std::nullopt;
			}
			sound.pcmBuffer.resize(chunk.size);
			file.read(reinterpret_cast<char*>(sound.pcmBuffer.data()), chunk.size);
			dataFound = true;
		}
		// 未知のチャンクと奇数サイズのpaddingを飛ばす
		offset += paddedSize;
		file.seekg(static_cast<std::streamoff>(offset));
		if (!file) {
			error = "WAVチャンクの読込に失敗しました";
			return std::nullopt;
		}
	}
	if (!formatFound || !dataFound) {
		error = "WAVにfmtまたはdataチャンクがありません";
		return std::nullopt;
	}
	if (!ValidateWaveFormat(sound, error, requiresDecode)) { return std::nullopt; }
	return sound;
}
