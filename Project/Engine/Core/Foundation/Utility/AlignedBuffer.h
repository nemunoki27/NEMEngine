#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstddef>
#include <utility>

namespace Engine {

	//============================================================================
	//	AlignedBuffer struct
	//	指定されたバイト数、アライメントのバッファを管理
	//============================================================================
	struct AlignedBuffer {

		AlignedBuffer() = default;
		AlignedBuffer(size_t argBytes, size_t argAlign) { Reset(argBytes, argAlign); }
		~AlignedBuffer() { Release(); }

		// バッファへのポインタ
		std::byte* ptr = nullptr;
		// バイト数、アライメント
		size_t bytes = 0;
		size_t align = 0;

		// バッファを指定されたバイト数、アライメントで再確保する
		void Reset(size_t argBytes, size_t argAlign);
		// バッファを解放して、状態をリセットする
		void Release();

		// 既存のバイト列を維持して容量と整列を確保する
		void Reserve(size_t requiredBytes, size_t requiredAlign, size_t preservedBytes);

		// コピー禁止
		AlignedBuffer(const AlignedBuffer&) = delete;
		AlignedBuffer& operator=(const AlignedBuffer&) = delete;
		// ムーブ許可
		AlignedBuffer(AlignedBuffer&& other) noexcept { *this = std::move(other); }
		AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;
	};

}
