#include "ECSStorage.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	BlobStore classMethods
//============================================================================
Engine::BlobStore::Handle Engine::BlobStore::Acquire(std::span<const std::byte> bytes) {

	const uint64_t hash = Hash(bytes);
	const auto [begin, end] = hashToHandle_.equal_range(hash);
	for (auto it = begin; it != end; ++it) {

		// Hash衝突時は内容まで比較し、異なるBlobを誤共有しない
		Entry* entry = entries_.TryGet(it->second);
		if (entry && entry->bytes.size() == bytes.size() &&
			std::equal(entry->bytes.begin(), entry->bytes.end(), bytes.begin())) {

			++entry->referenceCount;
			return it->second;
		}
	}

	Entry entry{};
	entry.bytes.assign(bytes.begin(), bytes.end());
	entry.hash = hash;
	entry.referenceCount = 1;
	const Handle handle = entries_.Emplace(std::move(entry));
	hashToHandle_.emplace(hash, handle);
	return handle;
}

bool Engine::BlobStore::Release(Handle handle) {

	Entry* entry = entries_.TryGet(handle);
	if (!entry) {
		return false;
	}
	if (1 < entry->referenceCount) {

		--entry->referenceCount;
		return true;
	}

	const auto [begin, end] = hashToHandle_.equal_range(entry->hash);
	for (auto it = begin; it != end; ++it) {
		if (it->second == handle) {

			hashToHandle_.erase(it);
			break;
		}
	}
	return entries_.Release(handle);
}

void Engine::BlobStore::Clear() {

	hashToHandle_.clear();
	entries_.Clear();
}

std::span<const std::byte> Engine::BlobStore::Get(Handle handle) const {

	const Entry* entry = entries_.TryGet(handle);
	return entry ? std::span<const std::byte>(entry->bytes) : std::span<const std::byte>{};
}

bool Engine::BlobStore::IsAlive(Handle handle) const {

	return entries_.IsAlive(handle);
}

uint32_t Engine::BlobStore::GetReferenceCount(Handle handle) const {

	const Entry* entry = entries_.TryGet(handle);
	return entry ? entry->referenceCount : 0;
}

uint64_t Engine::BlobStore::Hash(std::span<const std::byte> bytes) {

	uint64_t hash = 14695981039346656037ull;
	for (std::byte value : bytes) {

		hash ^= std::to_integer<uint8_t>(value);
		hash *= 1099511628211ull;
	}
	return hash;
}

//============================================================================
//	ECSStorageRegistry classMethods
//============================================================================
void Engine::ECSStorageRegistry::Clear() {

	storages_.clear();
}
