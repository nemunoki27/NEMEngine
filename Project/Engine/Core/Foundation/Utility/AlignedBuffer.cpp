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

//============================================================================
//	AlignedBuffer structMethods
//============================================================================
void Engine::AlignedBuffer::Reset(size_t argBytes, size_t argAlign) {

	if (argAlign == 0 || (argAlign & (argAlign - 1)) != 0) {
		throw std::invalid_argument("Bufferのアライメントが不正です");
	}
	std::byte* candidate = static_cast<std::byte*>(::operator new(argBytes, std::align_val_t(argAlign)));
	// 確保に成功してから旧領域を置き換える
	Release();
	bytes = argBytes;
	align = argAlign;
	ptr = candidate;
}

void Engine::AlignedBuffer::Release() {

	if (ptr) {
		::operator delete(ptr, std::align_val_t(align));
		ptr = nullptr;
	}
	bytes = 0;
	align = 0;
}

Engine::AlignedBuffer& Engine::AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {

	if (this == &other) {
		return *this;
	}
	Release();
	ptr = other.ptr;
	bytes = other.bytes;
	align = other.align;
	other.ptr = nullptr;
	other.bytes = 0;
	other.align = 0;
	return *this;
}

void Engine::AlignedBuffer::Reserve(size_t requiredBytes, size_t requiredAlign, size_t preservedBytes) {

	if (preservedBytes > bytes || requiredAlign == 0 || (requiredAlign & (requiredAlign - 1)) != 0) {
		throw std::invalid_argument("Bufferの拡張条件が不正です");
	}
	if (requiredBytes <= bytes && requiredAlign <= align) {
		return;
	}
	const size_t grownBytes = bytes <= SIZE_MAX / 2 ? bytes * 2 : bytes;
	AlignedBuffer candidate((std::max)(requiredBytes, grownBytes), (std::max)(requiredAlign, align));
	// コピーを終えてから旧領域を解放する
	if (preservedBytes != 0) {
		std::memcpy(candidate.ptr, ptr, preservedBytes);
	}
	*this = std::move(candidate);
}
