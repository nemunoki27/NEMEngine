#include "ProductBinary.h"

#include <Windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")

namespace Engine::ProductBinary {

	std::vector<uint8_t> CompressSmaller(const std::vector<uint8_t>& bytes) {

		if (bytes.size() < 256 || bytes.size() > 0x7fffffffULL) return {};
		std::vector<uint8_t> packed(bytes.size() - 1);
		COMPRESSOR_HANDLE encoder{};
		if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &encoder)) throw std::runtime_error("圧縮器を作成できません");
		SIZE_T written = 0;
		const bool ok = Compress(encoder, bytes.data(), bytes.size(), packed.data(), packed.size(), &written) != FALSE;
		const DWORD error = GetLastError();
		CloseCompressor(encoder);
		if (!ok) {
			if (error == ERROR_INSUFFICIENT_BUFFER) return {};
			throw std::runtime_error("製品データを圧縮できません");
		}
		packed.resize(written);
		return packed;
	}

	bool Decompress(const std::vector<uint8_t>& packed, size_t size, std::vector<uint8_t>& bytes) {

		std::vector<uint8_t> result(size);
		DECOMPRESSOR_HANDLE decoder{};
		if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &decoder)) return false;
		SIZE_T written = 0;
		const bool ok = ::Decompress(decoder, packed.data(), packed.size(), result.data(), size, &written) != FALSE;
		CloseDecompressor(decoder);
		if (!ok || written != size) return false;
		bytes.swap(result);
		return true;
	}
}
