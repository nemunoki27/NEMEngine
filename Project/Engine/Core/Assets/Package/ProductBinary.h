#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace Engine::ProductBinary {

	std::vector<uint8_t> CompressSmaller(const std::vector<uint8_t>& bytes);
	bool Decompress(const std::vector<uint8_t>& packed, size_t size, std::vector<uint8_t>& bytes);

	class Writer {
	public:
		explicit Writer(std::vector<uint8_t>& bytes) : bytes_(bytes) {}

		void Copy(const void* data, size_t size) {
			if (!size) return;
			const auto* first = static_cast<const uint8_t*>(data);
			bytes_.insert(bytes_.end(), first, first + size);
		}

		uint32_t Count(size_t size) {
			if (size > UINT32_MAX) throw std::length_error("製品データの要素数が上限を超えています");
			auto count = static_cast<uint32_t>(size);
			Copy(&count, sizeof(count));
			return count;
		}
	private:
		std::vector<uint8_t>& bytes_;
	};

	class Reader {
	public:
		explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

		void Copy(void* data, size_t size) {
			if (size > bytes_.size()) throw std::runtime_error("製品データの範囲が無効です");
			if (size) std::memcpy(data, bytes_.data(), size);
			bytes_ = bytes_.subspan(size);
		}

		uint32_t Count(size_t) {
			uint32_t count{};
			Copy(&count, sizeof(count));
			if (count > bytes_.size()) throw std::runtime_error("製品データの要素数が無効です");
			return count;
		}

		size_t Remaining() const { return bytes_.size(); }
	private:
		std::span<const uint8_t> bytes_;
	};

	template<class A, class... Values>
	void Scalars(A& archive, Values&... values) {
		static_assert((std::is_trivially_copyable_v<Values> && ...));
		(archive.Copy(&values, sizeof(values)), ...);
	}

	template<class A>
	void String(A& archive, std::string& value) {
		const auto count = archive.Count(value.size());
		if constexpr (std::is_same_v<A, Reader>) value.resize(count);
		archive.Copy(value.data(), count);
	}

	template<class A, class T>
	void Array(A& archive, std::vector<T>& values) {
		static_assert(std::is_trivially_copyable_v<T>);
		const auto count = archive.Count(values.size());
		const auto size = static_cast<size_t>(count) * sizeof(T);
		if constexpr (std::is_same_v<A, Reader>) {
			if (size > archive.Remaining()) throw std::runtime_error("製品データの配列長が無効です");
			values.resize(count);
		}
		archive.Copy(values.data(), size);
	}

	template<class A, class T, class F>
	void Sequence(A& archive, std::vector<T>& values, F&& serialize) {
		const auto count = archive.Count(values.size());
		for (uint32_t i = 0; i < count; ++i) {
			if constexpr (std::is_same_v<A, Reader>) serialize(values.emplace_back());
			else serialize(values[i]);
		}
	}
}
