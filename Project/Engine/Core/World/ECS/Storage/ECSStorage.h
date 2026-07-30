#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
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
			// 解放、再利用を区別する世代
			uint32_t generation = 1;
			// 次の空きスロット
			uint32_t nextFree = (std::numeric_limits<uint32_t>::max)();
		};

		//--------- variables ----------------------------------------------------

		static constexpr uint32_t kInvalidIndex = (std::numeric_limits<uint32_t>::max)();

		// データを保持するスロット
		std::vector<Slot> slots_;
		// 最初の空きスロット
		uint32_t freeHead_ = kInvalidIndex;
		// 生存データ数
		uint32_t aliveCount_ = 0;
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
		Handle Acquire(std::span<const std::byte> bytes);
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
			std::vector<std::byte> bytes;
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

		std::span<const T> Get(const void* root) const;
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
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<std::byte> bytes_;
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
		~ECSStorageRegistry() = default;

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

	uint32_t index = 0;
	if (freeHead_ != kInvalidIndex) {

		// 解放済みスロットを再利用し、ハンドルのindexを安定した小整数に保つ
		index = freeHead_;
		Slot& slot = slots_[index];
		freeHead_ = slot.nextFree;
		slot.nextFree = kInvalidIndex;
		slot.value.emplace(std::forward<Args>(args)...);
	} else {

		index = static_cast<uint32_t>(slots_.size());
		Slot& slot = slots_.emplace_back();
		slot.value.emplace(std::forward<Args>(args)...);
	}

	++aliveCount_;
	return Handle{ index, slots_[index].generation };
}

template <typename T, typename Tag>
inline bool Engine::GenerationalPool<T, Tag>::Release(Handle handle) {

	if (!IsAlive(handle)) {
		return false;
	}

	Slot& slot = slots_[handle.index];
	slot.value.reset();
	// 同じindexを再利用しても古いハンドルが通らないよう世代を進める
	++slot.generation;
	if (slot.generation == 0) {
		++slot.generation;
	}
	slot.nextFree = freeHead_;
	freeHead_ = handle.index;
	--aliveCount_;
	return true;
}

template <typename T, typename Tag>
inline void Engine::GenerationalPool<T, Tag>::Clear() {

	slots_.clear();
	freeHead_ = kInvalidIndex;
	aliveCount_ = 0;
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

	return handle.IsValid() &&
		handle.index < slots_.size() &&
		slots_[handle.index].generation == handle.generation &&
		slots_[handle.index].value.has_value();
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
		buffer->assign(values.begin(), values.end());
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
	return Acquire(std::as_bytes(std::span<const T>(&value, 1)));
}

template <typename T>
inline const T* Engine::BlobStore::TryGetObject(Handle handle) const {

	const std::span<const std::byte> bytes = Get(handle);
	if (bytes.size() < sizeof(T)) {
		return nullptr;
	}
	return reinterpret_cast<const T*>(bytes.data());
}

//============================================================================
//	BlobAsset structuresTemplateMethods
//============================================================================
template <typename T>
inline std::span<const T> Engine::BlobArray<T>::Get(const void* root) const {

	if (!root || count == 0) {
		return {};
	}
	const std::byte* base = static_cast<const std::byte*>(root);
	return { reinterpret_cast<const T*>(base + offset), count };
}

//============================================================================
//	BlobBuilder classTemplateMethods
//============================================================================
template <typename Root>
inline Engine::BlobBuilder<Root>::BlobBuilder() {

	static_assert(std::is_trivially_copyable_v<Root>);
	bytes_.resize(sizeof(Root));
	std::fill(bytes_.begin(), bytes_.end(), std::byte{ 0 });
}

template <typename Root>
inline void Engine::BlobBuilder<Root>::SetRoot(const Root& root) {

	std::memcpy(bytes_.data(), &root, sizeof(Root));
}

template <typename Root>
template <typename T>
inline Engine::BlobArray<T> Engine::BlobBuilder<Root>::AddArray(
	std::span<const T> values) {

	static_assert(std::is_trivially_copyable_v<T>);
	const size_t alignedOffset =
		(bytes_.size() + alignof(T) - 1) & ~(alignof(T) - 1);
	bytes_.resize(alignedOffset + values.size_bytes());
	if (!values.empty()) {
		std::memcpy(bytes_.data() + alignedOffset,
			values.data(), values.size_bytes());
	}

	BlobArray<T> result{};
	result.offset = static_cast<uint32_t>(alignedOffset);
	result.count = static_cast<uint32_t>(values.size());
	return result;
}

template <typename Root>
inline Engine::BlobAssetReference<Root> Engine::BlobBuilder<Root>::Build(
	BlobStore& store) const {

	// 内容Hashが一致するBlobは共有され、同じColliderやRender配列の重複を避ける
	return BlobAssetReference<Root>{ store.Acquire(bytes_) };
}

template <typename Root>
inline Root& Engine::BlobBuilder<Root>::GetRoot() {

	return *reinterpret_cast<Root*>(bytes_.data());
}

template <typename Root>
inline const Root& Engine::BlobBuilder<Root>::GetRoot() const {

	return *reinterpret_cast<const Root*>(bytes_.data());
}

//============================================================================
//	ECSStorageRegistry classTemplateMethods
//============================================================================
template <typename Storage>
inline Storage& Engine::ECSStorageRegistry::Get() {

	const void* typeKey = GetTypeKey<Storage>();
	auto it = storages_.find(typeKey);
	if (it == storages_.end()) {

		auto entry = std::make_unique<StorageEntry<Storage>>();
		Storage* value = &entry->value;
		storages_.emplace(typeKey, std::move(entry));
		return *value;
	}
	return static_cast<StorageEntry<Storage>*>(it->second.get())->value;
}

template <typename Storage>
inline Storage* Engine::ECSStorageRegistry::TryGet() {

	const void* typeKey = GetTypeKey<Storage>();
	auto it = storages_.find(typeKey);
	return it != storages_.end() ?
		&static_cast<StorageEntry<Storage>*>(it->second.get())->value : nullptr;
}

template <typename Storage>
inline const Storage* Engine::ECSStorageRegistry::TryGet() const {

	const void* typeKey = GetTypeKey<Storage>();
	auto it = storages_.find(typeKey);
	return it != storages_.end() ?
		&static_cast<const StorageEntry<Storage>*>(it->second.get())->value : nullptr;
}

template <typename Storage>
inline const void* Engine::ECSStorageRegistry::GetTypeKey() {

	static const uint8_t key = 0;
	return &key;
}
