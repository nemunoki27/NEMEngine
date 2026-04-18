#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/ECS/Entity/EntityArchetype.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <array>
#include <utility>

namespace Engine {

	//============================================================================
	//	ECSWorld structures
	//============================================================================

	// エンティティの位置を表す構造体
	struct EntityLocation {

		EntityArchetype* archetype = nullptr;
		uint32_t chunkIndex = 0;
		uint32_t row = 0;
	};
	// エンティティIDからエンティティの位置や世代を管理する構造体
	struct EntityRecord {

		uint32_t generation = 0;
		bool alive = false;

		// シーン側の永続UUID
		UUID uuid{};

		// どのArchetype/Chunk/Rowにいるか
		EntityLocation location{};
	};

	//============================================================================
	//	ECSWorld class
	//	シーンを構成するエンティティとコンポーネントを管理するクラス
	//============================================================================
	class ECSWorld {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ECSWorld();
		~ECSWorld() = default;

		//========================================================================
		//	エンティティに対して行う操作
		//========================================================================

		// エンティティの作成
		Entity CreateEntity(UUID stableUUID = UUID{});
		// エンティティの破棄
		void DestroyEntity(const Entity& entity);

		//========================================================================
		//	コンポーネントに対して行う操作
		//========================================================================

		// エンティティにコンポーネントを追加
		template <typename T>
		T& AddComponent(const Entity& entity);
		bool AddComponentByName(const Entity& entity, const std::string_view& typeName);
		// エンティティからコンポーネントを削除
		template <typename T>
		void RemoveComponent(const Entity& entity);
		bool RemoveComponentByName(const Entity& entity, const std::string_view& typeName);

		// jsonからエンティティに対してコンポーネントを追加
		void AddComponentFromJson(const Entity& entity, const std::string_view& typeName, const nlohmann::json& data);
		// エンティティのコンポーネントをjsonに変換
		void SerializeEntityComponents(const Entity& entity, nlohmann::json& outComponents) const;
		bool SerializeComponentToJson(const Entity& entity, const std::string_view& typeName, nlohmann::json& outData) const;

		//========================================================================
		//	ヘルパー
		//========================================================================

		// シグネチャにマッチするエンティティ全てに対して関数を呼び出す
		template <typename... T, typename Fn>
		void ForEach(Fn&& fn);
		template <typename Fn>
		void ForEachAliveEntity(Fn&& fn);

		//--------- accessor -----------------------------------------------------

		// エンティティが存在するか
		bool IsAlive(const Entity& entity) const;
		// エンティティのUUIDを返す
		UUID GetUUID(const Entity& entity) const;
		// UUIDからエンティティを検索する
		Entity FindByUUID(UUID id) const;

		// コンポーネントを持っているか
		template <typename T>
		bool HasComponent(const Entity& entity) const;
		bool HasComponent(const Entity& entity, const std::string_view& typeName) const;
		// エンティティのコンポーネントを返す
		template <typename T>
		T& GetComponent(const Entity& entity);

		// 現在レコードされているエンティティの数を返す
		uint32_t GetRecordCount() const { return static_cast<uint32_t>(records_.size()); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// エンティティIDからエンティティの位置や世代を管理する配列
		std::vector<EntityRecord> records_;
		// 破棄されたエンティティIDの再利用のための空きIDのスタック
		std::vector<uint32_t> free_;
		// シーン側の永続UUIDからエンティティIDへのマップ
		std::unordered_map<UUID, Entity> uuidToEntity_;

		// シグネチャからArchetypeへのマップ
		std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash> archetypes_;

		// 空のArchetype。コンポーネントを持たないエンティティはここにまとめる
		EntityArchetype* emptyArchetype_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 新しいエンティティIDを割り当てる
		uint32_t AllocateIndex();
		// エンティティが存在することを確認する。存在しない場合はアサート
		void AssertAlive(const Entity& entity) const;

		// エンティティが所属するArchetypeを移動する本体
		void MigrateEntity(const Entity& entity, const EntitySignature& oldSignature, const EntitySignature& newSignature);
		// シグネチャにマッチするArchetypeがあれば返し、なければ作成して返す
		EntityArchetype* GetOrCreateArchetype(const EntitySignature& signature);

		// Archetype上のtypeID配列から列番号配列を一度だけ解決する
		template <size_t N, size_t... I>
		static std::array<uint32_t, N> ResolveColumnIndices(EntityArchetype* archetype,
			const std::array<uint32_t, N>& typeIDs, std::index_sequence<I...>);
		// 行ごとにコンポーネント取得を挟まず、列へ直接アクセスして走査する
		template <typename... T, typename Fn, size_t... I>
		static void ForEachChunkFast(EntityChunk& chunk, const std::array<uint32_t, sizeof...(T)>& columnIndices,
			Fn& fn, std::index_sequence<I...>);
	};

	//============================================================================
	//	ECSWorld templateMethods
	//============================================================================

	template <typename T>
	inline T& ECSWorld::AddComponent(const Entity& entity) {

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();

		// 既に持ってるなら参照をそのまま返す
		if (HasComponent<T>(entity)) {
			return GetComponent<T>(entity);
		}

		// 新しいシグネチャを作るために古いシグネチャを取ってくる
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		EntitySignature newSignature = oldSignature;
		newSignature.Set(typeID);
		// シグネチャを更新してArchetypeを移動する
		MigrateEntity(entity, oldSignature, newSignature);

		// 移動後の場所からコンポーネント参照を返す
		return GetComponent<T>(entity);
	}

	template <typename T>
	inline void ECSWorld::RemoveComponent(const Entity& entity) {

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		if (!HasComponent<T>(entity)) {
			return;
		}

		// 新しいシグネチャを作るために古いシグネチャを取ってくる
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		EntitySignature newSignature = oldSignature;
		newSignature.Reset(typeID);

		// シグネチャを更新してArchetypeを移動する
		MigrateEntity(entity, oldSignature, newSignature);
	}

	template <typename ...T, typename Fn>
	inline void ECSWorld::ForEach(Fn&& fn) {

		// 必要な型IDは最初に一度だけ取得する
		ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
		const std::array<uint32_t, sizeof...(T)> requiredTypeIDs = { registry.GetID<T>()... };

		EntitySignature required{};
		for (uint32_t typeID : requiredTypeIDs) {
			required.Set(typeID);
		}

		// 関数を同一実体のまま全チャンクで再利用する
		Fn& fnRef = fn;
		for (auto& [signature, archPtr] : archetypes_) {

			EntityArchetype* archetype = archPtr.get();
			// シグネチャが必要なコンポーネントを全て含んでいるか
			if (!archetype->GetSignature().Contains(required)) {
				continue;
			}

			// このArchetypeに対する列番号を一度だけ解決する
			const std::array<uint32_t, sizeof...(T)> columnIndices = ResolveColumnIndices(
				archetype, requiredTypeIDs, std::index_sequence_for<T...>{});

			// チャンクを走査
			for (auto& chunk : archetype->GetChunks()) {

				// レコード再検索を挟まず直接列アクセスする
				ForEachChunkFast<T...>(*chunk, columnIndices, fnRef, std::index_sequence_for<T...>{});
			}
		}
	}

	template <typename Fn>
	inline void ECSWorld::ForEachAliveEntity(Fn&& fn) {

		for (uint32_t i = 0; i < records_.size(); ++i) {
			if (!records_[i].alive) {
				continue;
			}
			Entity entity{ i, records_[i].generation };

			// 関数を呼び出す
			fn(entity);
		}
	}

	template <typename T>
	inline bool ECSWorld::HasComponent(const Entity& entity) const {

		// エンティティが存在するか
		if (!IsAlive(entity)) {
			return false;
		}
		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		return records_[entity.index].location.archetype->Has(typeID);
	}

	template <typename T>
	inline T& ECSWorld::GetComponent(const Entity& entity) {

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
		return *(T*)ptr;
	}

	template <size_t N, size_t... I>
	inline std::array<uint32_t, N> ECSWorld::ResolveColumnIndices(EntityArchetype* archetype,
		const std::array<uint32_t, N>& typeIDs, std::index_sequence<I...>) {

		return { archetype->GetColumnIndex(typeIDs[I])... };
	}

	template <typename... T, typename Fn, size_t... I>
	inline void ECSWorld::ForEachChunkFast(EntityChunk& chunk, const std::array<uint32_t, sizeof...(T)>& columnIndices,
		Fn& fn, std::index_sequence<I...>) {

		const auto& entities = chunk.GetEntities();
		const uint32_t count = chunk.GetCount();
		for (uint32_t row = 0; row < count; ++row) {

			// 行ごとに直接コンポーネント参照を取り出してコールバックを呼ぶ
			fn(entities[row], (*reinterpret_cast<T*>(chunk.GetRawByColumnIndex(columnIndices[I], row)))...);
		}
	}
} // Engine