#include "ContentHash.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <vector>
// windows
#include <Windows.h>
#include <bcrypt.h>

//============================================================================
//	ContentHash classMethods
//============================================================================
namespace {

	class SHA256Context {
	public:

		SHA256Context() {

			if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
				&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
				return;
			}

			ULONG resultSize = 0;
			if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm_,
				BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize_),
				sizeof(objectSize_), &resultSize, 0)) ||
				!BCRYPT_SUCCESS(BCryptGetProperty(algorithm_,
					BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize_),
					sizeof(hashSize_), &resultSize, 0))) {
				return;
			}

			object_.resize(objectSize_);
			if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_,
				object_.data(), static_cast<ULONG>(object_.size()),
				nullptr, 0, 0))) {
				hash_ = nullptr;
			}
		}

		~SHA256Context() {

			if (hash_) {
				BCryptDestroyHash(hash_);
			}
			if (algorithm_) {
				BCryptCloseAlgorithmProvider(algorithm_, 0);
			}
		}

		bool IsValid() const {

			return algorithm_ && hash_ && hashSize_ > 0;
		}

		bool Update(const void* data, size_t size) {

			const auto* bytes = static_cast<const uint8_t*>(data);
			while (size > 0) {

				const ULONG chunkSize = static_cast<ULONG>(
					(std::min)(size,
						static_cast<size_t>((std::numeric_limits<ULONG>::max)())));
				if (!BCRYPT_SUCCESS(BCryptHashData(hash_,
					const_cast<PUCHAR>(bytes), chunkSize, 0))) {
					return false;
				}
				bytes += chunkSize;
				size -= chunkSize;
			}
			return true;
		}

		std::string Finish() {

			if (!IsValid()) {
				return {};
			}
			std::vector<uint8_t> hash(hashSize_);
			if (!BCRYPT_SUCCESS(BCryptFinishHash(
				hash_, hash.data(), hashSize_, 0))) {
				return {};
			}

			static constexpr char kHex[] = "0123456789abcdef";
			std::string result(hash.size() * 2, '0');
			for (size_t index = 0; index < hash.size(); ++index) {
				result[index * 2] = kHex[hash[index] >> 4];
				result[index * 2 + 1] = kHex[hash[index] & 0x0f];
			}
			return result;
		}
	private:

		BCRYPT_ALG_HANDLE algorithm_ = nullptr;
		BCRYPT_HASH_HANDLE hash_ = nullptr;
		ULONG objectSize_ = 0;
		ULONG hashSize_ = 0;
		std::vector<uint8_t> object_;
	};
}

std::string Engine::ContentHash::SHA256(std::span<const uint8_t> bytes) {

	SHA256Context context;
	if (!context.IsValid() || !context.Update(bytes.data(), bytes.size())) {
		return {};
	}
	return context.Finish();
}

std::string Engine::ContentHash::FileSHA256(
	const std::filesystem::path& path) {

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return {};
	}

	SHA256Context context;
	if (!context.IsValid()) {
		return {};
	}

	std::array<char, 64 * 1024> buffer{};
	while (file) {

		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		if (!context.Update(buffer.data(), static_cast<size_t>(file.gcount()))) {
			return {};
		}
	}
	return file.eof() ? context.Finish() : std::string{};
}
