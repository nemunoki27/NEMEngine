#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstddef>

namespace Engine {

	//============================================================================
	//	AlignedBuffer class
	//	指定されたバイト数、アライメントのバッファを管理
	//============================================================================
	class AlignedBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AlignedBuffer() = default;
		AlignedBuffer(size_t bytes, size_t alignment);
		~AlignedBuffer();
		AlignedBuffer(const AlignedBuffer&) = delete;
		AlignedBuffer& operator=(const AlignedBuffer&) = delete;
		AlignedBuffer(AlignedBuffer&& other) noexcept;
		AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;

		// バッファを指定されたバイト数、アライメントで再確保する
		void Reset(size_t argBytes, size_t argAlign);
		// バッファを解放して、状態をリセットする
		void Release();

		// 既存のバイト列を維持して容量と整列を確保する
		void Reserve(size_t requiredBytes, size_t requiredAlign, size_t preservedBytes);

		//--------- accessor -----------------------------------------------------

		// 所有領域を読み書きする
		std::byte* GetData() { return data_; }
		const std::byte* GetData() const { return data_; }
		// 確保済みのバイト数と整列を返す
		size_t GetSize() const { return bytes_; }
		size_t GetAlignment() const { return alignment_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::byte* data_ = nullptr; // 所有する領域
		size_t bytes_ = 0;          // 確保済みのバイト数
		size_t alignment_ = 0;      // 確保時の整列
	};

}
