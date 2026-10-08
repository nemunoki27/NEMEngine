#include "DynamicBuffer.h"

//============================================================================
//	DynamicBuffer classMethods
//============================================================================

namespace Engine {

	UntypedDynamicBuffer::UntypedDynamicBuffer(DynamicBufferHeader* header,
		size_t elementSize, size_t elementAlign, bool triviallyCopyable) :
		header_(header),
		elementSize_(elementSize),
		elementAlign_(elementAlign),
		triviallyCopyable_(triviallyCopyable) {

	}
}
//============================================================================
//	DynamicBuffer classMethods
//============================================================================

bool Engine::UntypedDynamicBuffer::SetData(
	const void* data, uint32_t count) {

	if (!IsValid() || !triviallyCopyable_ || (count != 0 && !data)) {
		return false;
	}
	// 自分の要素列を指定した場合は範囲と重なりを保護する
	const uintptr_t source = reinterpret_cast<uintptr_t>(data);
	const uintptr_t begin = reinterpret_cast<uintptr_t>(GetData());
	const size_t bytes = elementSize_ * header_->size;
	const bool internalSource = data && GetData() && source >= begin && source - begin <= bytes;
	const size_t offset = internalSource ? source - begin : 0;
	if (internalSource && (offset % elementSize_ != 0 || count > (bytes - offset) / elementSize_)) {
		return false;
	}
	if (!Resize(count)) {
		return false;
	}
	if (count != 0) {
		const void* sourceData = internalSource ? static_cast<const std::byte*>(GetData()) + offset : data;
		std::memmove(GetData(), sourceData, elementSize_ * count);
	}
	return true;
}

bool Engine::UntypedDynamicBuffer::SetElement(
	uint32_t index, const void* data) {

	if (!IsValid() || !triviallyCopyable_ || !data ||
		index >= header_->size) {
		return false;
	}
	std::memmove(
		static_cast<std::byte*>(header_->data) + elementSize_ * index,
		data, elementSize_);
	return true;
}

bool Engine::UntypedDynamicBuffer::RemoveAt(uint32_t index) {

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

bool Engine::UntypedDynamicBuffer::Resize(uint32_t size) {

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

uint32_t Engine::ReadOnlyUntypedDynamicBuffer::CopyTo(
	void* destination, uint32_t capacity, uint32_t startIndex) const {

	if (!IsValid() || !triviallyCopyable_ ||
		startIndex >= header_->size) {
		return 0;
	}
	const uint32_t copyCount =
		(std::min)(header_->size - startIndex, capacity);
	if (copyCount != 0 && destination) {
		// 同じBuffer内への重なったコピーも保護する
		std::memmove(
			destination,
			static_cast<const std::byte*>(GetData()) +
			elementSize_ * startIndex,
			elementSize_ * copyCount);
	}
	return copyCount;
}

bool Engine::UntypedDynamicBuffer::Reserve(uint32_t capacity) {

	if (!IsValid() || !triviallyCopyable_) {
		return false;
	}
	if (capacity <= header_->capacity) {
		return true;
	}

	// 逐次追加で毎回確保せず、積算サイズのあふれも拒否する
	const size_t maxCapacity = (std::min)((std::numeric_limits<size_t>::max)() / elementSize_,
		static_cast<size_t>((std::numeric_limits<uint32_t>::max)()));
	if (capacity > maxCapacity) {
		return false;
	}
	const size_t doubled = static_cast<size_t>(header_->capacity) * 2;
	capacity = static_cast<uint32_t>((std::max)(static_cast<size_t>(capacity), (std::min)(doubled, maxCapacity)));
	void* destination = nullptr;
	try {
		destination = ::operator new(elementSize_ * capacity, std::align_val_t(elementAlign_));
	} catch (const std::bad_alloc&) {
		return false;
	}
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

void* Engine::UntypedDynamicBuffer::GetInternalData() const {

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

Engine::ReadOnlyUntypedDynamicBuffer::ReadOnlyUntypedDynamicBuffer(const DynamicBufferHeader* header,
	size_t elementSize, size_t elementAlign, bool triviallyCopyable) :
	header_(header), elementSize_(elementSize), elementAlign_(elementAlign), triviallyCopyable_(triviallyCopyable) {
}

Engine::ReadOnlyUntypedDynamicBuffer Engine::UntypedDynamicBuffer::GetReadOnly() const {

	return { header_, elementSize_, elementAlign_, triviallyCopyable_ };
}

uint32_t Engine::UntypedDynamicBuffer::CopyTo(void* destination, uint32_t capacity, uint32_t startIndex) const {

	return GetReadOnly().CopyTo(destination, capacity, startIndex);
}
