#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/ECS/Behavior/BehaviorHandle.h>
#include <Engine/Core/ECS/Entity/Entity.h>

// c++
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	BehaviorWorld structures
	//============================================================================

	// ビヘイビアの実体を保持する構造体
	struct BehaviorRecord {

		// 世代管理用
		uint32_t generation = 0;
		// 型
		uint32_t typeID = 0;
		// 生存フラグ
		bool alive = false;

		// 所持しているエンティティ
		Entity owner = Entity::Null();
		// 実体
		std::unique_ptr<MonoBehavior> instance;

		// 状態
		bool enabled = false;
		bool awakeCalled = false;
		bool startCalled = false;
		// スイープ用
		bool seen = false;
	};

	//============================================================================
	//	BehaviorWorld class
	//	ワールドごとのビヘイビアの実体を保持するストア
	//============================================================================
	class BehaviorWorld {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BehaviorWorld() = default;
		~BehaviorWorld() = default;

		// ビヘイビアの作成
		BehaviorHandle Create(uint32_t typeID, const Entity& owner);
		// ビヘイビアの破棄
		void Destroy(const BehaviorHandle& handle, ECSWorld& world, const  SystemContext& context);
		// ビヘイビアの全ての実体を破棄
		void DestroyAll(ECSWorld& world, const  SystemContext& context);

		// 全てのビヘイビアの実体に対してアクセスされたフラグをクリアする
		void ClearSeenFlags();
		// 実体がアクセスされなかったビヘイビアを全てのレコードに対して破棄する
		void SweepUnseen(ECSWorld& world, const SystemContext& context);

		// ビヘイビアの実体全てに対して関数を呼び出す
		template <typename Fn>
		void ForEachAlive(Fn&& fn);
		// 指定Entityが所有するビヘイビアの実体に対して関数を呼び出す
		template <typename Fn>
		void ForEachAliveByOwner(const Entity& owner, Fn&& fn);

		//--------- accessor -----------------------------------------------------

		// ハンドルが有効で、かつ生存しているか
		bool IsAlive(const BehaviorHandle& handle) const;

		// ビヘイビアのレコード情報を返す
		BehaviorRecord* GetRecord(const BehaviorHandle& handle);
		const BehaviorRecord* GetRecord(const BehaviorHandle& handle) const;

		// ビヘイビアの実体を返す
		MonoBehavior* GetBehavior(const BehaviorHandle& handle);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<BehaviorRecord> records_;
		std::vector<uint32_t> free_;
		std::unordered_map<uint64_t, std::vector<uint32_t>> ownerToRecords_;

		//--------- functions ----------------------------------------------------

		/// 新しいビヘイビアIDを割り当てる
		uint32_t AllocateIndex();
		// Entityを検索用キーへ変換する
		static uint64_t MakeOwnerKey(const Entity& owner);
		// 指定したインデックスのビヘイビアを破棄する
		void DestroyIndex(uint32_t index, ECSWorld& world, const SystemContext& context);

		// レコード情報の数を返す
		uint32_t GetRecordCount() const { return static_cast<uint32_t>(records_.size()); }
	};

	//============================================================================
	//	BehaviorWorld templateMethods
	//============================================================================

	template<typename Fn>
	inline void BehaviorWorld::ForEachAlive(Fn&& fn) {

		for (auto& record : records_) {
			if (!record.alive) {
				continue;
			}
			fn(record);
		}
	}

	template<typename Fn>
	inline void BehaviorWorld::ForEachAliveByOwner(const Entity& owner, Fn&& fn) {

		auto it = ownerToRecords_.find(MakeOwnerKey(owner));
		if (it == ownerToRecords_.end()) {
			return;
		}

		for (uint32_t index : it->second) {
			if (records_.size() <= index) {
				continue;
			}
			BehaviorRecord& record = records_[index];
			if (!record.alive || record.owner != owner) {
				continue;
			}
			fn(record);
		}
	}
} // Engine
