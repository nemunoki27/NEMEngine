#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/AlignedBuffer.h>

// c++
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	StorageHandle struct
	//	チャンク外データを参照する世代付きハンドル
	//============================================================================
	template <typename Tag>
	struct StorageHandle {

		// 参照インデックス
		uint32_t index = (std::numeric_limits<uint32_t>::max)();
		// 解放、再利用を区別する世代
		uint32_t generation = 0;

		bool operator==(const StorageHandle& other) const noexcept = default;

		//--------- accessor -----------------------------------------------------

		// 有効なハンドルか
		constexpr bool IsValid() const noexcept;
		// 無効なハンドルを返す
		static constexpr StorageHandle Null() noexcept;
	};

	//============================================================================
	//	GenerationalPool class
	//	世代検証付きでデータを所有するプール
	//============================================================================
	template <typename T, typename Tag>
	class GenerationalPool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		using Handle = StorageHandle<Tag>;

		GenerationalPool() = default;
		~GenerationalPool() { Clear(); }
		GenerationalPool(const GenerationalPool&) = delete;
		GenerationalPool& operator=(const GenerationalPool&) = delete;
		GenerationalPool(GenerationalPool&&) = delete;
		GenerationalPool& operator=(GenerationalPool&&) = delete;

		// データを構築してハンドルを返す
		template <typename... Args>
		Handle Emplace(Args&&... args);
		// ハンドルが参照するデータを解放する
		bool Release(Handle handle);
		// 全データを解放する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// ハンドルが参照するデータを返す
		T* TryGet(Handle handle);
		const T* TryGet(Handle handle) const;
		// ハンドルが有効か
		bool IsAlive(Handle handle) const;
		// 生存データ数を返す
		uint32_t GetAliveCount() const { return aliveCount_; }
		// 確保済みスロット数を返す
		uint32_t GetSlotCount() const { return static_cast<uint32_t>(slots_.size()); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct Slot {

			// 所有するデータ
			std::optional<T> value;
			// 構築済みで参照を公開しているか
			bool alive = false;
			// 解放、再利用を区別する世代
			uint32_t generation = 1;
			// 次の空きスロット
			uint32_t nextFree = (std::numeric_limits<uint32_t>::max)();
		};

		//--------- variables ----------------------------------------------------

		static constexpr uint32_t kInvalidIndex = (std::numeric_limits<uint32_t>::max)();

		// データを保持するスロット
		std::deque<Slot> slots_;
		// 最初の空きスロット
		uint32_t freeHead_ = kInvalidIndex;
		// 生存データ数
		uint32_t aliveCount_ = 0;
		// 構築と破棄の再入深度
		size_t operationDepth_ = 0;
		// 全解放中の再生成を拒否する
		bool clearing_ = false;

		//--------- functions ----------------------------------------------------

		// 参照を無効化してからデータを破棄する
		void DestroySlot(uint32_t index);
	};

	//============================================================================
	//	RuntimeBufferPool class
	//	エンティティごとの可変長配列をチャンク外へ保持するプール
	//============================================================================
	template <typename T, typename Tag>
	class RuntimeBufferPool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		using Handle = StorageHandle<Tag>;

		// 配列を作成してハンドルを返す
		Handle Create(std::span<const T> values = {});
		// ハンドルが参照する配列を解放する
		bool Release(Handle handle);
		// 配列の内容を置き換える
		void Assign(Handle handle, std::span<const T> values);
		// 配列の要素数を変更する
		void Resize(Handle handle, size_t size);
		// 全配列を解放する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// ハンドルが有効か
		bool IsAlive(Handle handle) const;
		// 配列を返す
		std::span<T> Get(Handle handle);
		std::span<const T> Get(Handle handle) const;
		// 配列の実体を返す
		std::vector<T>* TryGetVector(Handle handle);
		const std::vector<T>* TryGetVector(Handle handle) const;
		// 生存配列数を返す
		uint32_t GetAliveCount() const { return pool_.GetAliveCount(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 可変長配列の所有先
		GenerationalPool<std::vector<T>, Tag> pool_;
	};

	//============================================================================
	//	BlobStore class
	//	同じ内容を共有する変更不可のバイト列
	//============================================================================
	class BlobStore {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		struct BlobTag;
		using Handle = StorageHandle<BlobTag>;

		BlobStore() = default;
		~BlobStore() = default;

		// バイト列を取得または作成して参照数を増やす
		Handle Acquire(std::span<const std::byte> bytes, size_t alignment = alignof(std::max_align_t));
		// オブジェクトをバイト列として取得または作成する
		template <typename T>
		Handle AcquireObject(const T& value);
		// 参照数を減らして未参照なら解放する
		bool Release(Handle handle);
		// 全バイト列を解放する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// バイト列を返す
		std::span<const std::byte> Get(Handle handle) const;
		// 先頭オブジェクトを型付きで返す
		template <typename T>
		const T* TryGetObject(Handle handle) const;
		// ハンドルが有効か
		bool IsAlive(Handle handle) const;
		// 参照数を返す
		uint32_t GetReferenceCount(Handle handle) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct Entry {

			// 変更不可のバイト列
			AlignedBuffer bytes;
			// 内容のハッシュ値
			uint64_t hash = 0;
			// 共有している参照数
			uint32_t referenceCount = 0;
		};

		//--------- variables ----------------------------------------------------

		// バイト列の所有先
		GenerationalPool<Entry, BlobTag> entries_;
		// ハッシュ値から候補を引くテーブル
		std::unordered_multimap<uint64_t, Handle> hashToHandle_;

		//--------- functions ----------------------------------------------------

		// バイト列のハッシュ値を返す
		static uint64_t Hash(std::span<const std::byte> bytes);
	};

	//============================================================================
	//	BlobAsset structures
	//============================================================================
	// BlobStore内の変更不可データを型付きで参照する
	template <typename T>
	struct BlobAssetReference {

		BlobStore::Handle handle{};

		bool IsValid() const { return handle.IsValid(); }
		bool operator==(const BlobAssetReference&) const noexcept = default;
	};

	// Blobルートからの相対位置で配列を参照する
	template <typename T>
	struct BlobArray {

		uint32_t offset = 0;
		uint32_t count = 0;

		std::span<const T> Get(std::span<const std::byte> bytes) const;
	};

	//============================================================================
	//	BlobBuilder class
	//	ルート構造体と可変長配列を単一Blobへ構築する
	//============================================================================
	template <typename Root>
	class BlobBuilder {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BlobBuilder();
		~BlobBuilder() = default;
		BlobBuilder(const BlobBuilder& other);
		BlobBuilder& operator=(const BlobBuilder& other);
		BlobBuilder(BlobBuilder&& other) noexcept;
		BlobBuilder& operator=(BlobBuilder&& other) noexcept;

		// ルート構造体を設定する
		void SetRoot(const Root& root);
		// 配列をBlob末尾へ追加して相対参照を返す
		template <typename T>
		BlobArray<T> AddArray(std::span<const T> values);
		// BlobStoreへ登録して型付き参照を返す
		BlobAssetReference<Root> Build(BlobStore& store) const;

		//--------- accessor -----------------------------------------------------

		Root& GetRoot();
		const Root& GetRoot() const;
		std::span<const std::byte> GetBytes() const { return { bytes_.ptr, size_ }; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AlignedBuffer bytes_;
		size_t size_ = 0;

		// 移動後のBuilderを空のルートから再利用する
		void EnsureRoot();
	};

	//============================================================================
	//	ECSStorageRegistry class
	//	ワールドに属するチャンク外データの所有先
	//============================================================================
	class ECSStorageRegistry {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ECSStorageRegistry() = default;
		~ECSStorageRegistry();

		// 全ストレージを解放する
		void Clear();

		//--------- accessor -----------------------------------------------------

		// 指定型のストレージを取得または作成する
		template <typename Storage>
		Storage& Get();
		// 指定型のストレージがあれば返す
		template <typename Storage>
		Storage* TryGet();
		template <typename Storage>
		const Storage* TryGet() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct IStorage {

			virtual ~IStorage() = default;
		};

		template <typename Storage>
		struct StorageEntry final :
			IStorage {

			Storage value;
		};

		//--------- variables ----------------------------------------------------

		// 型ごとのストレージ
		std::unordered_map<const void*, std::unique_ptr<IStorage>> storages_;
		size_t constructionDepth_ = 0;
		bool clearing_ = false;

		//--------- functions ----------------------------------------------------

		// 型を識別するアドレスを返す
		template <typename Storage>
		static const void* GetTypeKey();
	};
} // Engine

//============================================================================
//	StorageHandle structTemplateMethods
//============================================================================
template <typename Tag>
inline constexpr bool Engine::StorageHandle<Tag>::IsValid() const noexcept {

	return index != (std::numeric_limits<uint32_t>::max)();
}

template <typename Tag>
inline constexpr Engine::StorageHandle<Tag> Engine::StorageHandle<Tag>::Null() noexcept {

	return {};
}

//============================================================================
//	GenerationalPool classTemplateMethods
//============================================================================
template <typename T, typename Tag>
template <typename... Args>
inline Engine::GenerationalPool<T, Tag>::Handle Engine::GenerationalPool<T, Tag>::Emplace(Args&&... args) {

	static_assert(std::is_nothrow_destructible_v<T>);
	if (clearing_) {
		throw std::logic_error("全解放中のストレージへ追加できません");
	}

	uint32_t index = freeHead_;
	if (index != kInvalidIndex) {

		// 構築中の再入から予約枠を隠す
		freeHead_ = slots_[index].nextFree;
		slots_[index].nextFree = kInvalidIndex;
	} else {

		if (slots_.size() >= kInvalidIndex) {
			throw std::length_error("ストレージの枠数が上限に達しました");
		}
		index = static_cast<uint32_t>(slots_.size());
		slots_.emplace_back();
	}

	Slot& slot = slots_[index];
	++operationDepth_;
	try {
		slot.value.emplace(std::forward<Args>(args)...);
	} catch (...) {

		// 生成失敗の枠を現在の空き列へ戻す
		--operationDepth_;
		slot.nextFree = freeHead_;
		freeHead_ = index;
		throw;
	}
	--operationDepth_;
	slot.alive = true;
	++aliveCount_;
	return Handle{ index, slot.generation };
}

template <typename T, typename Tag>
inline bool Engine::GenerationalPool<T, Tag>::Release(Handle handle) {

	if (!IsAlive(handle)) {
		return false;
	}
	DestroySlot(handle.index);
	return true;
}

template <typename T, typename Tag>
inline void Engine::GenerationalPool<T, Tag>::DestroySlot(uint32_t index) {

	Slot& slot = slots_[index];
	// 破棄処理から古い参照を取得させない
	slot.alive = false;
	--aliveCount_;
	slot.generation = slot.generation == kInvalidIndex ? 0 : slot.generation + 1;
	++operationDepth_;
	slot.value.reset();
	--operationDepth_;

	// 世代を使い切った枠は再利用しない
	if (slot.generation != 0) {
		slot.nextFree = freeHead_;
		freeHead_ = index;
	}
}

template <typename T, typename Tag>
inline void Engine::GenerationalPool<T, Tag>::Clear() {

	if (clearing_) {
		return;
	}
	if (operationDepth_ != 0) {
		throw std::logic_error("構築または破棄の途中でストレージを全解放できません");
	}

	// 世代履歴を残し、全解放後の再接続を防ぐ
	clearing_ = true;
	for (uint32_t index = 0; index < slots_.size(); ++index) {
		if (slots_[index].alive) {
			DestroySlot(index);
		}
	}
	clearing_ = false;
}

template <typename T, typename Tag>
inline T* Engine::GenerationalPool<T, Tag>::TryGet(Handle handle) {

	return IsAlive(handle) ? &*slots_[handle.index].value : nullptr;
}

template <typename T, typename Tag>
inline const T* Engine::GenerationalPool<T, Tag>::TryGet(Handle handle) const {

	return IsAlive(handle) ? &*slots_[handle.index].value : nullptr;
}

template <typename T, typename Tag>
inline bool Engine::GenerationalPool<T, Tag>::IsAlive(Handle handle) const {

	return !clearing_ && handle.IsValid() &&
		handle.index < slots_.size() &&
		slots_[handle.index].generation == handle.generation &&
		slots_[handle.index].alive;
}

//============================================================================
//	RuntimeBufferPool classTemplateMethods
//============================================================================
template <typename T, typename Tag>
inline Engine::RuntimeBufferPool<T, Tag>::Handle Engine::RuntimeBufferPool<T, Tag>::Create(
	std::span<const T> values) {

	return pool_.Emplace(values.begin(), values.end());
}

template <typename T, typename Tag>
inline bool Engine::RuntimeBufferPool<T, Tag>::Release(Handle handle) {

	return pool_.Release(handle);
}

template <typename T, typename Tag>
inline void Engine::RuntimeBufferPool<T, Tag>::Assign(Handle handle, std::span<const T> values) {

	if (std::vector<T>* buffer = pool_.TryGet(handle)) {
		// コピー完了まで旧配列と自己参照の入力を保持する
		std::vector<T> candidate(values.begin(), values.end());
		buffer->swap(candidate);
	}
}

template <typename T, typename Tag>
inline void Engine::RuntimeBufferPool<T, Tag>::Resize(Handle handle, size_t size) {

	if (std::vector<T>* buffer = pool_.TryGet(handle)) {
		buffer->resize(size);
	}
}

template <typename T, typename Tag>
inline void Engine::RuntimeBufferPool<T, Tag>::Clear() {

	pool_.Clear();
}

template <typename T, typename Tag>
inline bool Engine::RuntimeBufferPool<T, Tag>::IsAlive(Handle handle) const {

	return pool_.IsAlive(handle);
}

template <typename T, typename Tag>
inline std::span<T> Engine::RuntimeBufferPool<T, Tag>::Get(Handle handle) {

	std::vector<T>* values = pool_.TryGet(handle);
	return values ? std::span<T>(*values) : std::span<T>{};
}

template <typename T, typename Tag>
inline std::span<const T> Engine::RuntimeBufferPool<T, Tag>::Get(Handle handle) const {

	const std::vector<T>* values = pool_.TryGet(handle);
	return values ? std::span<const T>(*values) : std::span<const T>{};
}

template <typename T, typename Tag>
inline std::vector<T>* Engine::RuntimeBufferPool<T, Tag>::TryGetVector(Handle handle) {

	return pool_.TryGet(handle);
}

template <typename T, typename Tag>
inline const std::vector<T>* Engine::RuntimeBufferPool<T, Tag>::TryGetVector(Handle handle) const {

	return pool_.TryGet(handle);
}

//============================================================================
//	BlobStore classTemplateMethods
//============================================================================
template <typename T>
inline Engine::BlobStore::Handle Engine::BlobStore::AcquireObject(const T& value) {

	static_assert(std::is_trivially_copyable_v<T>);
	return Acquire(std::as_bytes(std::span<const T>(&value, 1)), alignof(T));
}

template <typename T>
inline const T* Engine::BlobStore::TryGetObject(Handle handle) const {

	const std::span<const std::byte> bytes = Get(handle);
	if (bytes.size() < sizeof(T) || reinterpret_cast<uintptr_t>(bytes.data()) % alignof(T) != 0) {
		return nullptr;
	}
	return reinterpret_cast<const T*>(bytes.data());
}

//============================================================================
//	BlobAsset structuresTemplateMethods
//============================================================================
template <typename T>
inline std::span<const T> Engine::BlobArray<T>::Get(std::span<const std::byte> bytes) const {

	if (count == 0 || offset > bytes.size() || count > (bytes.size() - offset) / sizeof(T)) {
		return {};
	}
	const std::byte* data = bytes.data() + offset;
	if (reinterpret_cast<uintptr_t>(data) % alignof(T) != 0) {
		return {};
	}
	return { reinterpret_cast<const T*>(data), count };
}

//============================================================================
//	BlobBuilder classTemplateMethods
//============================================================================
template <typename Root>
inline Engine::BlobBuilder<Root>::BlobBuilder() {

	static_assert(std::is_trivially_copyable_v<Root> && sizeof(Root) <= UINT32_MAX);
	EnsureRoot();
}

template <typename Root>
inline Engine::BlobBuilder<Root>::BlobBuilder(const BlobBuilder& other) :
	bytes_((std::max)(sizeof(Root), other.size_), (std::max)(alignof(Root), other.bytes_.align)),
	size_((std::max)(sizeof(Root), other.size_)) {

	if (other.size_ != 0) {
		std::memcpy(bytes_.ptr, other.bytes_.ptr, size_);
	} else {
		std::memset(bytes_.ptr, 0, size_);
	}
}

template <typename Root>
inline Engine::BlobBuilder<Root>::BlobBuilder(BlobBuilder&& other) noexcept :
	bytes_(std::move(other.bytes_)), size_(std::exchange(other.size_, 0)) {
}

template <typename Root>
inline Engine::BlobBuilder<Root>& Engine::BlobBuilder<Root>::operator=(BlobBuilder&& other) noexcept {

	if (this != &other) {
		bytes_ = std::move(other.bytes_);
		size_ = std::exchange(other.size_, 0);
	}
	return *this;
}

template <typename Root>
inline void Engine::BlobBuilder<Root>::EnsureRoot() {

	if (size_ == 0) {
		bytes_.Reset(sizeof(Root), alignof(Root));
		size_ = sizeof(Root);
		std::memset(bytes_.ptr, 0, size_);
	}
}

template <typename Root>
inline Engine::BlobBuilder<Root>& Engine::BlobBuilder<Root>::operator=(const BlobBuilder& other) {

	if (this != &other) {
		BlobBuilder candidate(other);
		*this = std::move(candidate);
	}
	return *this;
}

template <typename Root>
inline void Engine::BlobBuilder<Root>::SetRoot(const Root& root) {

	EnsureRoot();
	std::memmove(bytes_.ptr, &root, sizeof(Root));
}

template <typename Root>
template <typename T>
inline Engine::BlobArray<T> Engine::BlobBuilder<Root>::AddArray(
	std::span<const T> values) {

	static_assert(std::is_trivially_copyable_v<T>);
	EnsureRoot();
	const size_t padding = (alignof(T) - size_ % alignof(T)) % alignof(T);
	if (padding > UINT32_MAX - size_ || values.size() > UINT32_MAX ||
		values.size() > (UINT32_MAX - size_ - padding) / sizeof(T)) {
		throw std::length_error("Blob配列のサイズが上限を超えています");
	}
	const size_t alignedOffset = size_ + padding;
	const size_t newSize = alignedOffset + values.size_bytes();
	const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(values.data());
	const uintptr_t beginAddress = reinterpret_cast<uintptr_t>(bytes_.ptr);
	const bool internalSource = !values.empty() && sourceAddress >= beginAddress && sourceAddress - beginAddress < size_;
	const size_t sourceOffset = internalSource ? sourceAddress - beginAddress : 0;
	if (internalSource && values.size_bytes() > size_ - sourceOffset) {
		throw std::out_of_range("Blob配列のコピー元が領域を超えています");
	}
	bytes_.Reserve(newSize, alignof(T), size_);
	// 再確保した内部入力を引き直して末尾へ追加する
	if (!values.empty()) {
		const void* source = internalSource ? bytes_.ptr + sourceOffset : static_cast<const void*>(values.data());
		std::memcpy(bytes_.ptr + alignedOffset, source, values.size_bytes());
	}
	std::memset(bytes_.ptr + size_, 0, padding);
	size_ = newSize;

	BlobArray<T> result{};
	result.offset = static_cast<uint32_t>(alignedOffset);
	result.count = static_cast<uint32_t>(values.size());
	return result;
}

template <typename Root>
inline Engine::BlobAssetReference<Root> Engine::BlobBuilder<Root>::Build(
	BlobStore& store) const {

	if (size_ == 0) {
		throw std::logic_error("移動済みのBlobBuilderは公開できません");
	}
	// 内容Hashが一致するBlobは共有され、同じColliderやRender配列の重複を避ける
	return BlobAssetReference<Root>{ store.Acquire(std::span<const std::byte>(bytes_.ptr, size_), bytes_.align) };
}

template <typename Root>
inline Root& Engine::BlobBuilder<Root>::GetRoot() {

	EnsureRoot();
	return *reinterpret_cast<Root*>(bytes_.ptr);
}

template <typename Root>
inline const Root& Engine::BlobBuilder<Root>::GetRoot() const {

	if (size_ == 0) {
		throw std::logic_error("移動済みのBlobBuilderは参照できません");
	}
	return *reinterpret_cast<const Root*>(bytes_.ptr);
}

//============================================================================
//	ECSStorageRegistry classTemplateMethods
//============================================================================
template <typename Storage>
inline Storage& Engine::ECSStorageRegistry::Get() {

	static_assert(std::is_nothrow_destructible_v<Storage>);
	if (clearing_) {
		throw std::logic_error("終了中のStorageは作成できません");
	}
	const void* typeKey = GetTypeKey<Storage>();
	const auto [entry, inserted] = storages_.try_emplace(typeKey);
	if (!inserted) {
		if (!entry->second) {
			throw std::logic_error("同じStorageを構築中に再取得できません");
		}
		return static_cast<StorageEntry<Storage>*>(entry->second.get())->value;
	}

	// 構築中の別型登録で索引が再配置されても予約を維持する
	auto& reserved = entry->second;
	++constructionDepth_;
	try {
		reserved = std::make_unique<StorageEntry<Storage>>();
	} catch (...) {
		--constructionDepth_;
		storages_.erase(typeKey);
		throw;
	}
	--constructionDepth_;
	return static_cast<StorageEntry<Storage>*>(reserved.get())->value;
}

template <typename Storage>
inline Storage* Engine::ECSStorageRegistry::TryGet() {

	const void* typeKey = GetTypeKey<Storage>();
	auto it = storages_.find(typeKey);
	return it != storages_.end() && it->second ?
		&static_cast<StorageEntry<Storage>*>(it->second.get())->value : nullptr;
}

template <typename Storage>
inline const Storage* Engine::ECSStorageRegistry::TryGet() const {

	const void* typeKey = GetTypeKey<Storage>();
	auto it = storages_.find(typeKey);
	return it != storages_.end() && it->second ?
		&static_cast<const StorageEntry<Storage>*>(it->second.get())->value : nullptr;
}

template <typename Storage>
inline const void* Engine::ECSStorageRegistry::GetTypeKey() {

	static const uint8_t key = 0;
	return &key;
}
