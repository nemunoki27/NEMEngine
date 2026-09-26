#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Baking/RuntimeWorldBaker.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>

// c++
#include <array>
#include <functional>
#include <stdexcept>

namespace {

	// 構築と破棄の途中からPool操作を呼び出す
	class PoolValue {
	public:
		PoolValue(const std::function<void()>& construct, std::function<void()> destroy);
		~PoolValue();
	private:
		std::function<void()> destroy_;
	};

	PoolValue::PoolValue(const std::function<void()>& construct, std::function<void()> destroy) :
		destroy_(std::move(destroy)) {

		if (construct) {
			construct();
		}
	}

	PoolValue::~PoolValue() {

		if (destroy_) {
			destroy_();
		}
	}

	// 追加、構築失敗、破棄時の再入でも参照と空き枠を維持する
	bool CheckPoolLifetime() {

		struct PoolTag;
		using Pool = Engine::GenerationalPool<PoolValue, PoolTag>;
		Pool pool;
		bool failed = false;
		try {
			pool.Emplace([] { throw std::runtime_error("新規枠の構築失敗"); }, std::function<void()>{});
		} catch (const std::runtime_error&) {
			failed = true;
		}
		if (!failed || pool.GetSlotCount() != 1 || pool.GetAliveCount() != 0) {
			return false;
		}
		const auto first = pool.Emplace(std::function<void()>{}, std::function<void()>{});
		PoolValue* pointer = pool.TryGet(first);
		for (uint32_t index = 0; index < 2048; ++index) {
			pool.Emplace(std::function<void()>{}, std::function<void()>{});
		}
		if (pointer != pool.TryGet(first) || pool.GetAliveCount() != 2049) {
			return false;
		}
		pool.Clear();

		// 予約枠の構築中に別の枠を追加してから失敗させる
		Pool::Handle nested{};
		bool rejected = false;
		try {
			pool.Emplace([&] {
				nested = pool.Emplace(std::function<void()>{}, std::function<void()>{});
				throw std::runtime_error("構築失敗の検証");
			}, std::function<void()>{});
		} catch (const std::runtime_error&) {
			rejected = true;
		}
		if (!rejected || pool.GetAliveCount() != 1 || !pool.IsAlive(nested) || pool.IsAlive(first)) {
			return false;
		}

		// 破棄中は旧参照を隠し、追加先に破棄中の枠を使わない
		Pool::Handle owner{};
		Pool::Handle created{};
		bool hidden = false;
		owner = pool.Emplace(std::function<void()>{}, [&] {
			hidden = !pool.IsAlive(owner) && !pool.Release(owner);
			created = pool.Emplace(std::function<void()>{}, std::function<void()>{});
		});
		pool.Release(owner);
		if (!hidden || created.index == owner.index || !pool.IsAlive(created)) {
			return false;
		}

		// Clear中の生成を拒否し、破棄の再入を二重実行しない
		bool clearRejected = false;
		pool.Emplace(std::function<void()>{}, [&] {
			pool.Clear();
			try {
				pool.Emplace(std::function<void()>{}, std::function<void()>{});
			} catch (const std::logic_error&) {
				clearRejected = true;
			}
		});
		pool.Clear();
		const auto reused = pool.Emplace(std::function<void()>{}, std::function<void()>{});
		return clearRejected && pool.GetAliveCount() == 1 && pool.IsAlive(reused) &&
			!pool.IsAlive(nested) && !pool.IsAlive(owner) && !pool.IsAlive(created);
	}

	// 同じWorldを再登録しても以前のハンドルから解決しない
	bool CheckWorldHandles() {

		Engine::ECSWorld world;
		auto& registry = Engine::ManagedWorldRegistry::GetInstance();
		const auto first = registry.Register(world);
		registry.Unregister(first);
		const auto second = registry.Register(world);
		const auto existing = registry.Register(world);
		const bool passed = !registry.TryResolve(first) && registry.TryResolve(second) == &world &&
			second.index == existing.index && second.generation == existing.generation;
		registry.Unregister(second);
		if (!passed || registry.TryResolve(second)) {
			return false;
		}

		// 終了通知を保持してもWorld本体は延命しない
		Engine::RuntimeWorldBaker baker;
		Engine::ManagedWorldHandle destroyed{};
		std::shared_ptr<const Engine::ECSWorldLifetime> lifetime;
		{
			Engine::ECSWorld temporary(Engine::ECSWorldKind::Runtime);
			lifetime = temporary.GetLifetime();
			destroyed = registry.Register(temporary);
			baker.Attach(temporary, nullptr);
		}
		const bool expired = !lifetime->IsAlive() && !registry.TryResolve(destroyed) && !baker.IsAttached();
		baker.Flush();
		baker.Detach();
		registry.Unregister(destroyed);
		return expired;
	}

	struct alignas(128) AlignedBlobValue {

		int value = 0;
	};

	struct alignas(64) AlignedBlobRoot {

		Engine::BlobArray<AlignedBlobValue> values;
	};

	// ルートと配列の整列、内部入力の再確保を確認する
	bool CheckBlobAlignment() {

		Engine::BlobBuilder<AlignedBlobRoot> builder;
		const std::array<AlignedBlobValue, 2> values{ { { 43 }, { 71 } } };
		const auto first = builder.AddArray<AlignedBlobValue>(values);
		builder.GetRoot().values = first;
		const auto second = builder.AddArray<AlignedBlobValue>(first.Get(builder.GetBytes()));
		builder.GetRoot().values = second;
		Engine::BlobBuilder<AlignedBlobRoot> copy = builder;
		Engine::BlobBuilder<AlignedBlobRoot> moved = std::move(copy);
		Engine::BlobStore store;
		bool rejected = false;
		try {
			copy.Build(store);
		} catch (const std::logic_error&) {
			rejected = true;
		}
		if (!rejected || !copy.GetBytes().empty() || copy.GetRoot().values.count != 0) {
			return false;
		}
		copy = std::move(moved);
		const auto handle = copy.Build(store);
		const auto* root = store.TryGetObject<AlignedBlobRoot>(handle.handle);
		if (!root || reinterpret_cast<uintptr_t>(root) % alignof(AlignedBlobRoot) != 0) {
			return false;
		}
		const auto result = root->values.Get(store.Get(handle.handle));
		Engine::BlobArray<AlignedBlobValue> invalid = root->values;
		invalid.count = UINT32_MAX;
		if (!invalid.Get(store.Get(handle.handle)).empty()) {
			return false;
		}
		invalid = root->values;
		++invalid.offset;
		if (!invalid.Get(store.Get(handle.handle)).empty()) {
			return false;
		}
		return result.size() == 2 && reinterpret_cast<uintptr_t>(result.data()) % alignof(AlignedBlobValue) == 0 &&
			result[0].value == 43 && result[1].value == 71;
	}

	struct ReentrantStorage {

		static inline Engine::ECSStorageRegistry* owner = nullptr;
		static inline bool failConstruct = false;
		static inline bool rejectedRecursion = false;
		static inline bool hiddenOnDestroy = false;

		ReentrantStorage();
		~ReentrantStorage();
	};

	ReentrantStorage::ReentrantStorage() {

		if (failConstruct) {
			throw std::runtime_error("Storage構築失敗の検証");
		}
		try {
			owner->Get<ReentrantStorage>();
		} catch (const std::logic_error&) {
			rejectedRecursion = true;
		}
	}

	ReentrantStorage::~ReentrantStorage() {

		hiddenOnDestroy = owner->TryGet<ReentrantStorage>() == nullptr;
		owner->Clear();
	}

	// 構築の再入、失敗後の再作成、全解放中の検索を確認する
	bool CheckStorageRegistration() {

		Engine::ECSStorageRegistry storage;
		ReentrantStorage::owner = &storage;
		ReentrantStorage::failConstruct = true;
		bool failed = false;
		try {
			storage.Get<ReentrantStorage>();
		} catch (const std::runtime_error&) {
			failed = true;
		}
		ReentrantStorage::failConstruct = false;
		storage.Get<ReentrantStorage>();
		storage.Clear();
		ReentrantStorage::owner = nullptr;
		return failed && ReentrantStorage::rejectedRecursion && ReentrantStorage::hiddenOnDestroy;
	}

	struct RegistryValue {
		static constexpr bool kSerializable = false;
		int value = 0;
	};

	// 型登録の拒否後も公開済み情報と固定IDを維持する
	bool CheckTypeRegistration() {

		Engine::ComponentTypeRegistry registry;
		const auto* first = &registry.GetInfo(0);
		const uint32_t count = registry.GetComponentTypeCount();
		registry.Register<RegistryValue>(count, "RegistryValue");
		bool rejected = false;
		try {
			registry.Register<RegistryValue>(count + 1, "DuplicateRegistryValue");
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		return rejected && first == &registry.GetInfo(0) && registry.GetComponentTypeCount() == count + 1 &&
			registry.GetID<RegistryValue>() == count && !registry.FindByName("DuplicateRegistryValue");
	}
}

namespace NEMTests {

	bool TestECSExternalStorage() {

		struct TestBufferTag;
		Engine::RuntimeBufferPool<int32_t, TestBufferTag> buffers;
		const std::array<int32_t, 3> source = { 1, 2, 3 };
		const auto first = buffers.Create(source);
		if (!buffers.IsAlive(first) || buffers.Get(first).size() != source.size()) {
			return false;
		}
		if (!buffers.Release(first) || buffers.IsAlive(first)) {
			return false;
		}

		const auto second = buffers.Create(source);
		if (first.index != second.index || first.generation == second.generation ||
			!buffers.Get(first).empty()) {
			return false;
		}

		// 全解放後も旧ハンドルを新しい配列へ接続しない
		buffers.Clear();
		const auto third = buffers.Create(source);
		buffers.Assign(third, buffers.Get(third).subspan(1));
		if (buffers.Get(third).size() != 2 || buffers.Get(third)[0] != 2 || buffers.Get(third)[1] != 3) {
			return false;
		}
		if (buffers.IsAlive(first) || buffers.IsAlive(second) || !buffers.IsAlive(third)) {
			return false;
		}

		Engine::BlobStore blobs;
		const std::array<std::byte, 4> blobData = {
			std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 }
		};
		const Engine::BlobStore::Handle blobA = blobs.Acquire(blobData);
		const Engine::BlobStore::Handle blobB = blobs.Acquire(blobData);
		if (blobA != blobB || blobs.GetReferenceCount(blobA) != 2 ||
			blobs.Get(blobA).size() != blobData.size()) {
			return false;
		}
		if (!blobs.Release(blobA) || !blobs.IsAlive(blobB) ||
			blobs.GetReferenceCount(blobB) != 1) {
			return false;
		}
		if (!blobs.Release(blobB) || blobs.IsAlive(blobB)) {
			return false;
		}
		const auto blobC = blobs.Acquire(blobData);
		blobs.Clear();
		const auto blobD = blobs.Acquire(blobData);
		return !blobs.IsAlive(blobC) && blobs.IsAlive(blobD) && CheckPoolLifetime() &&
			CheckTypeRegistration() && CheckWorldHandles() && CheckBlobAlignment() && CheckStorageRegistration();
	}

}
