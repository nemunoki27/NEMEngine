#include "AlignedBuffer.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

//============================================================================
//	AlignedBuffer classMethods
//============================================================================
Engine::AlignedBuffer::AlignedBuffer(size_t bytes, size_t alignment) {

	Reset(bytes, alignment);
}

Engine::AlignedBuffer::AlignedBuffer(AlignedBuffer&& other) noexcept {

	*this = std::move(other);
}

Engine::AlignedBuffer::~AlignedBuffer() {

	Release();
}

void Engine::AlignedBuffer::Reset(size_t argBytes, size_t argAlign) {

	// 整列条件を確認
	if (argAlign == 0 || (argAlign & (argAlign - 1)) != 0) {
		throw std::invalid_argument("Bufferのアライメントが不正です");
	}
	std::byte* candidate = static_cast<std::byte*>(::operator new(argBytes, std::align_val_t(argAlign)));
	// 確保に成功してから旧領域を置き換える
	Release();
	bytes_ = argBytes;
	alignment_ = argAlign;
	data_ = candidate;
}

void Engine::AlignedBuffer::Release() {

	// 所有する領域を解放
	if (data_) {
		::operator delete(data_, std::align_val_t(alignment_));
		data_ = nullptr;
	}
	bytes_ = 0;
	alignment_ = 0;
}

Engine::AlignedBuffer& Engine::AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {

	if (this == &other) {
		return *this;
	}
	// 現在の領域を解放して所有を引き継ぐ
	Release();
	data_ = other.data_;
	bytes_ = other.bytes_;
	alignment_ = other.alignment_;
	// 移動元の所有を解除
	other.data_ = nullptr;
	other.bytes_ = 0;
	other.alignment_ = 0;
	return *this;
}

void Engine::AlignedBuffer::Reserve(size_t requiredBytes, size_t requiredAlign, size_t preservedBytes) {

	// コピー範囲と整列条件を確認
	if (preservedBytes > bytes_ || requiredAlign == 0 || (requiredAlign & (requiredAlign - 1)) != 0) {
		throw std::invalid_argument("Bufferの拡張条件が不正です");
	}
	// 現在の領域で足りれば再利用
	if (requiredBytes <= bytes_ && requiredAlign <= alignment_) {
		return;
	}
	const size_t grownBytes = bytes_ <= SIZE_MAX / 2 ? bytes_ * 2 : bytes_;
	AlignedBuffer candidate((std::max)(requiredBytes, grownBytes), (std::max)(requiredAlign, alignment_));
	// コピーを終えてから旧領域を解放する
	if (preservedBytes != 0) {
		std::memcpy(candidate.data_, data_, preservedBytes);
	}
	*this = std::move(candidate);
}
