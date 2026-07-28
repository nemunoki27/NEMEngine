#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <type_traits>

namespace Engine {

	//============================================================================
	//	DynamicBufferHeader struct
	//	チャンク内領域を優先して使用する可変長配列のヘッダ
	//============================================================================
	struct DynamicBufferHeader {

		// 現在の要素領域
		void* data = nullptr;
		// 要素数
		uint32_t size = 0;
		// 確保済み要素数
		uint32_t capacity = 0;
		// チャンク内に保持できる要素数
		uint32_t internalCapacity = 0;
	};

	//============================================================================
	//	UntypedDynamicBuffer class
	//	trivially copyableなBufferを実行時型情報から操作する
	//============================================================================
	class UntypedDynamicBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UntypedDynamicBuffer() = default;
		UntypedDynamicBuffer(DynamicBufferHeader* header,
			size_t elementSize, size_t elementAlign, bool triviallyCopyable) :
			header_(header),
			elementSize_(elementSize),
			elementAlign_(elementAlign),
			triviallyCopyable_(triviallyCopyable) {
		}
		~UntypedDynamicBuffer() = default;

		// 要素列を同一レイアウトのデータで置き換える
		bool SetData(const void* data, uint32_t count);
		// 指定位置の要素を置き換える
		bool SetElement(uint32_t index, const void* data);
		// 指定位置の要素を削除する
		bool RemoveAt(uint32_t index);
		// 要素数を変更して追加領域をゼロ初期化する
		bool Resize(uint32_t size);
		// 要素列を呼び出し側Bufferへコピーする
		uint32_t CopyTo(void* destination,
			uint32_t capacity, uint32_t startIndex = 0) const;

		//--------- accessor -----------------------------------------------------

		bool IsValid() const {
			return header_ && elementSize_ != 0 && elementAlign_ != 0;
		}
		bool IsTriviallyCopyable() const { return triviallyCopyable_; }
		uint32_t GetSize() const { return header_ ? header_->size : 0; }
		size_t GetElementSize() const { return elementSize_; }
		void* GetData() { return header_ ? header_->data : nullptr; }
		const void* GetData() const { return header_ ? header_->data : nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		DynamicBufferHeader* header_ = nullptr;
		size_t elementSize_ = 0;
		size_t elementAlign_ = 0;
		bool triviallyCopyable_ = false;

		//--------- functions ----------------------------------------------------

		bool Reserve(uint32_t capacity);
		void* GetInternalData() const;
		bool UsesInternalStorage() const;
	};

	//============================================================================
	//	DynamicBuffer class
	//	DynamicBufferHeaderを型付き配列として操作する
	//============================================================================
	template <typename T>
	class DynamicBuffer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DynamicBuffer() = default;
		explicit DynamicBuffer(DynamicBufferHeader* header) :
			header_(header) {
		}
		~DynamicBuffer() = default;

		// 要素を末尾へ追加する
		void Add(const T& value);
		// 要素を末尾へ構築する
		template <typename... Args>
		T& EmplaceBack(Args&&... args);
		// 指定位置を削除して後続要素を詰める
		void RemoveAt(uint32_t index);
		// 全要素を削除する
		void Clear();
		// 要素数を変更する
		void Resize(uint32_t size);
		// 必要な要素数を事前確保する
		void Reserve(uint32_t capacity);

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return header_ != nullptr; }
		bool IsEmpty() const { return GetSize() == 0; }
		uint32_t GetSize() const { return header_ ? header_->size : 0; }
		uint32_t GetCapacity() const { return header_ ? header_->capacity : 0; }
		T* GetData() { return header_ ? static_cast<T*>(header_->data) : nullptr; }
		const T* GetData() const { return header_ ? static_cast<const T*>(header_->data) : nullptr; }
		std::span<T> GetSpan() { return { GetData(), GetSize() }; }
		std::span<const T> GetSpan() const { return { GetData(), GetSize() }; }

		T& operator[](uint32_t index) {
			assert(index < GetSize());
			return GetData()[index];
		}
		const T& operator[](uint32_t index) const {
			assert(index < GetSize());
			return GetData()[index];
		}
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		DynamicBufferHeader* header_ = nullptr;

		//--------- functions ----------------------------------------------------

		bool UsesInternalStorage() const;
		T* GetInternalData() const;
	};

	//============================================================================
	//	DynamicBufferStorage namespace
	//	チャンク移動時のDynamicBufferライフサイクルを処理する
	//============================================================================
	namespace DynamicBufferStorage {

		// ヘッダとチャンク内領域を初期化する
		template <typename T>
		void Construct(void* storage, uint32_t internalCapacity);
		// 要素とチャンク外領域を破棄する
		template <typename T>
		void Destroy(void* storage);
		// 所有権を移動する
		template <typename T>
		void Move(void* destination, void* source);
		// 要素列を複製する
		template <typename T>
		void Copy(void* destination, const void* source);
	}
} // Engine

//============================================================================
//	UntypedDynamicBuffer classMethods
//============================================================================
inline bool Engine::UntypedDynamicBuffer::SetData(
	const void* data, uint32_t count) {

	if ((count != 0 && !data) || !Resize(count)) {
		return false;
	}
	if (count != 0) {
		std::memcpy(GetData(), data, elementSize_ * count);
	}
	return true;
}

inline bool Engine::UntypedDynamicBuffer::SetElement(
	uint32_t index, const void* data) {

	if (!IsValid() || !triviallyCopyable_ || !data ||
		index >= header_->size) {
		return false;
	}
	std::memcpy(
		static_cast<std::byte*>(header_->data) + elementSize_ * index,
		data, elementSize_);
	return true;
}

inline bool Engine::UntypedDynamicBuffer::RemoveAt(uint32_t index) {

	if (!IsValid() || !triviallyCopyable_ || index >= header_->size) {
		return false;
	}
	const uint32_t moveCount = header_->size - index - 1;
	if (moveCount != 0) {
		std::memmove(
			static_cast<std::byte*>(header_->data) + elementSize_ * index,
			static_cast<std::byte*>(header_->data) + elementSize_ * (index + 1),
			elementSize_ * moveCount);
	}
	--header_->size;
	return true;
}

inline bool Engine::UntypedDynamicBuffer::Resize(uint32_t size) {

	if (!IsValid() || !triviallyCopyable_) {
		return false;
	}
	const uint32_t oldSize = header_->size;
	if (size > header_->capacity && !Reserve(size)) {
		return false;
	}
	if (size > oldSize) {
		// C#側へ未初期化メモリを公開しないよう追加領域を初期化する
		std::memset(
			static_cast<std::byte*>(header_->data) + elementSize_ * oldSize,
			0, elementSize_ * (size - oldSize));
	}
	header_->size = size;
	return true;
}

inline uint32_t Engine::UntypedDynamicBuffer::CopyTo(
	void* destination, uint32_t capacity, uint32_t startIndex) const {

	if (!IsValid() || !triviallyCopyable_ ||
		startIndex >= header_->size) {
		return 0;
	}
	const uint32_t copyCount =
		(std::min)(header_->size - startIndex, capacity);
	if (copyCount != 0 && destination) {
		std::memcpy(
			destination,
			static_cast<const std::byte*>(GetData()) +
			elementSize_ * startIndex,
			elementSize_ * copyCount);
	}
	return copyCount;
}

inline bool Engine::UntypedDynamicBuffer::Reserve(uint32_t capacity) {

	if (!IsValid() || !triviallyCopyable_) {
		return false;
	}
	if (capacity <= header_->capacity) {
		return true;
	}

	void* destination = ::operator new(
		elementSize_ * capacity, std::align_val_t(elementAlign_));
	if (header_->size != 0) {
		std::memcpy(
			destination, header_->data, elementSize_ * header_->size);
	}
	if (!UsesInternalStorage()) {
		::operator delete(header_->data, std::align_val_t(elementAlign_));
	}
	header_->data = destination;
	header_->capacity = capacity;
	return true;
}

inline void* Engine::UntypedDynamicBuffer::GetInternalData() const {

	if (!header_) {
		return nullptr;
	}
	const uintptr_t headerEnd =
		reinterpret_cast<uintptr_t>(header_) + sizeof(DynamicBufferHeader);
	const uintptr_t aligned =
		(headerEnd + elementAlign_ - 1) &
		~(static_cast<uintptr_t>(elementAlign_) - 1);
	return reinterpret_cast<void*>(aligned);
}

inline bool Engine::UntypedDynamicBuffer::UsesInternalStorage() const {

	return header_ && header_->data == GetInternalData();
}

//============================================================================
//	DynamicBuffer classTemplateMethods
//============================================================================
template <typename T>
inline void Engine::DynamicBuffer<T>::Add(const T& value) {

	EmplaceBack(value);
}

template <typename T>
template <typename... Args>
inline T& Engine::DynamicBuffer<T>::EmplaceBack(Args&&... args) {

	assert(header_);
	if (header_->size == header_->capacity) {
		// チャンク内領域を使い切った時だけ外部領域へ拡張する
		Reserve((std::max)(1u, header_->capacity * 2u));
	}
	T* destination = GetData() + header_->size;
	new (destination) T(std::forward<Args>(args)...);
	++header_->size;
	return *destination;
}

template <typename T>
inline void Engine::DynamicBuffer<T>::RemoveAt(uint32_t index) {

	assert(header_);
	assert(index < header_->size);
	T* data = GetData();
	data[index].~T();
	for (uint32_t i = index; i + 1 < header_->size; ++i) {
		new (data + i) T(std::move(data[i + 1]));
		data[i + 1].~T();
	}
	--header_->size;
}

template <typename T>
inline void Engine::DynamicBuffer<T>::Clear() {

	if (!header_) {
		return;
	}
	T* data = GetData();
	for (uint32_t i = 0; i < header_->size; ++i) {
		data[i].~T();
	}
	header_->size = 0;
}

template <typename T>
inline void Engine::DynamicBuffer<T>::Resize(uint32_t size) {

	assert(header_);
	if (size < header_->size) {
		T* data = GetData();
		for (uint32_t i = size; i < header_->size; ++i) {
			data[i].~T();
		}
		header_->size = size;
		return;
	}
	if (header_->capacity < size) {
		Reserve(size);
	}
	T* data = GetData();
	for (uint32_t i = header_->size; i < size; ++i) {
		new (data + i) T();
	}
	header_->size = size;
}

template <typename T>
inline void Engine::DynamicBuffer<T>::Reserve(uint32_t capacity) {

	assert(header_);
	if (capacity <= header_->capacity) {
		return;
	}

	T* destination = static_cast<T*>(::operator new(
		sizeof(T) * capacity, std::align_val_t(alignof(T))));
	T* source = GetData();
	// 要素の順序を維持したまま新しい連続領域へ移動する
	for (uint32_t i = 0; i < header_->size; ++i) {
		new (destination + i) T(std::move(source[i]));
		source[i].~T();
	}
	if (!UsesInternalStorage()) {
		::operator delete(source, std::align_val_t(alignof(T)));
	}
	header_->data = destination;
	header_->capacity = capacity;
}

template <typename T>
inline bool Engine::DynamicBuffer<T>::UsesInternalStorage() const {

	return header_ && header_->data == GetInternalData();
}

template <typename T>
inline T* Engine::DynamicBuffer<T>::GetInternalData() const {

	const uintptr_t headerEnd =
		reinterpret_cast<uintptr_t>(header_) + sizeof(DynamicBufferHeader);
	const uintptr_t aligned =
		(headerEnd + alignof(T) - 1) & ~(static_cast<uintptr_t>(alignof(T)) - 1);
	return reinterpret_cast<T*>(aligned);
}

//============================================================================
//	DynamicBufferStorage namespaceTemplateMethods
//============================================================================
template <typename T>
inline void Engine::DynamicBufferStorage::Construct(void* storage, uint32_t internalCapacity) {

	DynamicBufferHeader* header = new (storage) DynamicBufferHeader();
	const uintptr_t headerEnd =
		reinterpret_cast<uintptr_t>(header) + sizeof(DynamicBufferHeader);
	const uintptr_t aligned =
		(headerEnd + alignof(T) - 1) & ~(static_cast<uintptr_t>(alignof(T)) - 1);
	header->data = reinterpret_cast<void*>(aligned);
	header->capacity = internalCapacity;
	header->internalCapacity = internalCapacity;
}

template <typename T>
inline void Engine::DynamicBufferStorage::Destroy(void* storage) {

	DynamicBufferHeader* header = static_cast<DynamicBufferHeader*>(storage);
	DynamicBuffer<T> buffer(header);
	T* data = buffer.GetData();
	buffer.Clear();

	const uintptr_t headerEnd =
		reinterpret_cast<uintptr_t>(header) + sizeof(DynamicBufferHeader);
	const uintptr_t aligned =
		(headerEnd + alignof(T) - 1) & ~(static_cast<uintptr_t>(alignof(T)) - 1);
	if (data && data != reinterpret_cast<T*>(aligned)) {
		::operator delete(data, std::align_val_t(alignof(T)));
	}
	header->~DynamicBufferHeader();
}

template <typename T>
inline void Engine::DynamicBufferStorage::Move(void* destination, void* source) {

	DynamicBufferHeader* sourceHeader = static_cast<DynamicBufferHeader*>(source);
	const uint32_t internalCapacity = sourceHeader->internalCapacity;
	Construct<T>(destination, internalCapacity);

	DynamicBufferHeader* destinationHeader =
		static_cast<DynamicBufferHeader*>(destination);
	const uintptr_t sourceHeaderEnd =
		reinterpret_cast<uintptr_t>(sourceHeader) + sizeof(DynamicBufferHeader);
	const uintptr_t sourceAligned =
		(sourceHeaderEnd + alignof(T) - 1) & ~(static_cast<uintptr_t>(alignof(T)) - 1);

	if (sourceHeader->data != reinterpret_cast<void*>(sourceAligned)) {

		// 外部領域は再確保せず所有権だけを移し、構造変更のコピー量を抑える
		destinationHeader->data = sourceHeader->data;
		destinationHeader->size = sourceHeader->size;
		destinationHeader->capacity = sourceHeader->capacity;
	} else {

		// チャンク内領域は移動先セルとアドレスが異なるため要素を移す
		DynamicBuffer<T> sourceBuffer(sourceHeader);
		DynamicBuffer<T> destinationBuffer(destinationHeader);
		destinationBuffer.Reserve(sourceHeader->size);
		for (T& value : sourceBuffer.GetSpan()) {
			destinationBuffer.EmplaceBack(std::move(value));
		}
		sourceBuffer.Clear();
	}

	sourceHeader->data = reinterpret_cast<void*>(sourceAligned);
	sourceHeader->size = 0;
	sourceHeader->capacity = internalCapacity;
}

template <typename T>
inline void Engine::DynamicBufferStorage::Copy(
	void* destination, const void* source) {

	const DynamicBufferHeader* sourceHeader =
		static_cast<const DynamicBufferHeader*>(source);
	Construct<T>(destination, sourceHeader->internalCapacity);

	DynamicBuffer<T> sourceBuffer(
		const_cast<DynamicBufferHeader*>(sourceHeader));
	DynamicBuffer<T> destinationBuffer(
		static_cast<DynamicBufferHeader*>(destination));
	destinationBuffer.Reserve(sourceHeader->size);
	for (const T& value : sourceBuffer.GetSpan()) {
		destinationBuffer.EmplaceBack(value);
	}
}
