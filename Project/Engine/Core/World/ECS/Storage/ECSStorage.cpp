#include "ECSStorage.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

//============================================================================
//	BlobStore classMethods
//============================================================================
Engine::BlobStore::Handle Engine::BlobStore::Acquire(std::span<const std::byte> bytes, size_t alignment) {

	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		throw std::invalid_argument("Blobのアライメントが不正です");
	}
	const uint64_t hash = Hash(bytes);
	const auto [begin, end] = hashToHandle_.equal_range(hash);
	for (auto it = begin; it != end; ++it) {

		// Hash衝突時は内容まで比較し、異なるBlobを誤共有しない
		Entry* entry = entries_.TryGet(it->second);
		if (entry && entry->bytes.bytes == bytes.size() && entry->bytes.align >= alignment &&
			std::equal(bytes.begin(), bytes.end(), entry->bytes.ptr)) {

			if (entry->referenceCount == (std::numeric_limits<uint32_t>::max)()) {
				throw std::overflow_error("Blobの参照数が上限に達しました");
			}
			++entry->referenceCount;
			return it->second;
		}
	}

	Entry entry{};
	entry.bytes.Reset(bytes.size(), alignment);
	if (!bytes.empty()) {
		std::memcpy(entry.bytes.ptr, bytes.data(), bytes.size());
	}
	entry.hash = hash;
	entry.referenceCount = 1;
	const Handle handle = entries_.Emplace(std::move(entry));
	// 索引の追加に失敗した候補は公開せず解放する
	try {
		hashToHandle_.emplace(hash, handle);
	} catch (...) {
		entries_.Release(handle);
		throw;
	}
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
	return entry ? std::span<const std::byte>(entry->bytes.ptr, entry->bytes.bytes) : std::span<const std::byte>{};
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
Engine::ECSStorageRegistry::~ECSStorageRegistry() {

	Clear();
}

void Engine::ECSStorageRegistry::Clear() {

	if (clearing_) {
		return;
	}
	if (constructionDepth_ != 0) {
		throw std::logic_error("構築中のStorageを全解放できません");
	}
	// 破棄中のStorageを検索対象から外す
	clearing_ = true;
	decltype(storages_) retired;
	retired.swap(storages_);
	retired.clear();
	clearing_ = false;
}
