#include "ContentHash.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <algorithm>
#include <array>
#include <fstream>
#include <vector>
// windows
#include <Windows.h>
#include <bcrypt.h>

//============================================================================
//	ContentHash classMethods
//============================================================================
namespace {

	//============================================================================
	//	SHA256Context class
	//	SHAの作成から破棄までを所有する
	//============================================================================
	class SHA256Context {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SHA256Context() = default;

		~SHA256Context() {

			// 作成できたHandleだけを逆順に解放する
			if (hash_) {
				BCryptDestroyHash(hash_);
			}
			if (algorithm_) {
				BCryptCloseAlgorithmProvider(algorithm_, 0);
			}
		}

		SHA256Context(const SHA256Context&) = delete;
		SHA256Context& operator=(const SHA256Context&) = delete;

		// 初期化途中の例外でもContextの所有資源を解放する
		bool Initialize() {

			// 鍵を使わないSHA-256を開く
			if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
				return false;
			}

			// Hashの管理領域と出力長を取得する
			ULONG resultSize = 0;
			if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize_),
					sizeof(objectSize_), &resultSize, 0)) ||
				!BCRYPT_SUCCESS(BCryptGetProperty(
					algorithm_, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize_), sizeof(hashSize_), &resultSize, 0))) {
				return false;
			}

			// Contextの所有領域にHashを作成する
			object_.resize(objectSize_);
			if (!BCRYPT_SUCCESS(
					BCryptCreateHash(algorithm_, &hash_, object_.data(), static_cast<ULONG>(object_.size()), nullptr, 0, 0))) {
				hash_ = nullptr;
			}
			return IsValid();
		}

		// 読み取り専用データをHashへ追加する
		bool Update(const void* data, size_t size) {

			const auto* bytes = static_cast<const uint8_t*>(data);
			std::array<uint8_t, 64 * 1024> buffer;
			while (size > 0) {

				// API入力へコピーして元データを保護する
				const ULONG chunkSize = static_cast<ULONG>((std::min)(size, buffer.size()));
				std::copy_n(bytes, chunkSize, buffer.data());
				if (!BCRYPT_SUCCESS(BCryptHashData(hash_, buffer.data(), chunkSize, 0))) {
					return false;
				}
				bytes += chunkSize;
				size -= chunkSize;
			}
			return true;
		}

		// 確定したHashを16進文字列へ変換する
		std::string Finish() {

			if (!IsValid()) {
				return {};
			}
			// Hashの出力を確定する
			std::vector<uint8_t> hash(hashSize_);
			if (!BCRYPT_SUCCESS(BCryptFinishHash(hash_, hash.data(), hashSize_, 0))) {
				return {};
			}

			// 1byteを2桁の16進表記にする
			static constexpr char kHex[] = "0123456789abcdef";
			std::string result(hash.size() * 2, '0');
			for (size_t index = 0; index < hash.size(); ++index) {
				result[index * 2] = kHex[hash[index] >> 4];
				result[index * 2 + 1] = kHex[hash[index] & 0x0f];
			}
			return result;
		}

		//--------- accessor -----------------------------------------------------

		// Hashの作成に成功したか取得する
		bool IsValid() const { return algorithm_ && hash_ && hashSize_ > 0; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 失敗途中のHandleも終了時まで保持する
		BCRYPT_ALG_HANDLE algorithm_ = nullptr;
		BCRYPT_HASH_HANDLE hash_ = nullptr;
		ULONG objectSize_ = 0;
		ULONG hashSize_ = 0;
		// BCryptが使用する管理領域
		std::vector<uint8_t> object_;
	};
}

std::string Engine::ContentHash::SHA256(std::span<const uint8_t> bytes) {

	// 呼出元のデータを変更せずHash化する
	SHA256Context context;
	if (!context.Initialize() || !context.Update(bytes.data(), bytes.size())) {
		return {};
	}
	return context.Finish();
}

std::string Engine::ContentHash::FileSHA256(const std::filesystem::path& path) {

	// ファイルを分割して読み込む
	std::ifstream file(Algorithm::ToFileSystemPath(path), std::ios::binary);
	if (!file.is_open()) {
		return {};
	}

	return ReadSHA256([&file](std::span<uint8_t> buffer, size_t& count) {

		file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
		count = static_cast<size_t>(file.gcount());
		return !file.bad() && (!file.fail() || file.eof());
	});
}

std::string Engine::ContentHash::ReadSHA256(const std::function<bool(std::span<uint8_t>, size_t&)>& read) {

	SHA256Context context;
	if (!read || !context.Initialize()) {
		return {};
	}
	// 読込元に依存せず同じ管理領域でHash化する
	std::array<uint8_t, 64 * 1024> buffer{};
	for (;;) {
		size_t count = 0;
		if (!read(buffer, count) || count > buffer.size()) {
			return {};
		}
		if (count == 0) {
			return context.Finish();
		}
		if (!context.Update(buffer.data(), count)) {
			return {};
		}
	}
}
