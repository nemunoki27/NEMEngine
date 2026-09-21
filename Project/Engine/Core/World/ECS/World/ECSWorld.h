#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Entity/EntityArchetype.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/World/WorldCommandBuffer.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <deque>
#include <span>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	ECSWorldKind enum
	//============================================================================
	enum class ECSWorldKind : uint8_t {

		Authoring,
		Runtime,
	};

	//============================================================================
	//	ECSWorld component mutation
	//============================================================================
	enum class ComponentMutationKind :
		uint8_t {

		Added,
		Removed,
		Modified,
		EntityDestroyed,
	};

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
		bool pendingDestroy = false;

		// シーン側の永続UUID
		UUID uuid{};

		// どのArchetype/Chunk/Rowにいるか
		EntityLocation location{};
	};

	//============================================================================
	//	ECSWorldStatistics struct
	//	ECSのメモリ使用量と構造変更量
	//============================================================================
	struct ECSWorldStatistics {

		// レコード、エンティティ、アーキタイプ、チャンク数
		uint32_t recordCount = 0;
		uint32_t aliveEntityCount = 0;
		uint32_t archetypeCount = 0;
		uint32_t chunkSlotCount = 0;
		uint32_t allocatedChunkCount = 0;
		// チャンクの確保量と使用量
		uint64_t allocatedChunkBytes = 0;
		uint64_t payloadBytes = 0;
		// 構造変更による移動量
		uint64_t structuralMigrationCount = 0;
		uint64_t relocatedComponentCount = 0;
		uint64_t relocatedComponentBytes = 0;
	};

	//============================================================================
	//	ECSWorld class
	//	シーンを構成するエンティティとコンポーネントを管理するクラス
	//============================================================================
	class ECSWorld {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit ECSWorld(ECSWorldKind kind = ECSWorldKind::Authoring);
		~ECSWorld();

		//============================================================================
		//	エンティティに対して行う操作
		//============================================================================
		// エンティティの作成
		Entity CreateEntity(UUID stableUUID = UUID{});
		// 最終シグネチャへ直接エンティティを作成
		Entity CreateEntityWithSignature(const EntitySignature& signature, UUID stableUUID = UUID{});
		// コンポーネント種類ID一覧から最終シグネチャへ直接エンティティを作成
		Entity CreateEntityWithComponents(std::span<const uint32_t> typeIDs, UUID stableUUID = UUID{});
		// エンティティの破棄をフレーム終端へ予約
		void DestroyEntity(const Entity& entity);
		// 予約済みの破棄をまとめて実行
		void FlushPendingDestroyEntities();

		//============================================================================
		//	スクリプト由来の構造変更を遅延適用するコマンドバッファ
		//============================================================================
		// scripting callbackからの構造変更はここへ積み、安全地点でFlushする
		WorldCommandBuffer& GetCommandBuffer() { return commandBuffer_; }
		// 積まれた構造変更コマンドをまとめて適用する
		void FlushWorldCommands() { commandBuffer_.Flush(*this); }

		// Prefab/SceneコマンドのFlush適用に必要な外部サービスでEngineApplicationが毎フレーム設定する
		void SetCommandServices(const WorldCommandServices& services) { commandServices_ = services; }
		const WorldCommandServices& GetCommandServices() const { return commandServices_; }

		//============================================================================
		//	コンポーネントに対して行う操作
		//============================================================================
		using ComponentMutationCallback = void(*)(
			ECSWorld&, const Entity&, uint32_t, ComponentMutationKind, void*);

		// エンティティにコンポーネントを追加
		template <typename T>
		T& AddComponent(const Entity& entity);
		bool AddComponentByName(const Entity& entity, const std::string_view& typeName);
		// エンティティからコンポーネントを削除
		template <typename T>
		void RemoveComponent(const Entity& entity);
		bool RemoveComponentByName(const Entity& entity, const std::string_view& typeName);
		// DynamicBufferを追加、削除する
		template <typename T>
		DynamicBuffer<T> AddBuffer(const Entity& entity);
		template <typename T>
		void RemoveBuffer(const Entity& entity);

		// jsonからエンティティに対してコンポーネントを追加
		void AddComponentFromJson(const Entity& entity, const std::string_view& typeName, const nlohmann::json& data);
		// 追加済みコンポーネントへjsonを適用
		bool ApplyComponentJson(const Entity& entity, const std::string_view& typeName, const nlohmann::json& data);
		// 保存対象Componentを同じEntity IDの独立Worldへ複製する
		std::unique_ptr<ECSWorld> CloneForSerialization() const;
		// エンティティのコンポーネントをjsonに変換
		void SerializeEntityComponents(const Entity& entity, nlohmann::json& outComponents) const;
		bool SerializeComponentToJson(const Entity& entity, const std::string_view& typeName, nlohmann::json& outData) const;

		// Componentの非構造的な値変更を購読側へ通知する
		template <typename T>
		void MarkComponentModified(const Entity& entity);
		void MarkComponentModified(const Entity& entity, uint32_t typeID);
		// 複数Componentの内部更新をまとめて描画抽出などへ通知する
		void MarkDataModified();
		// 描画構成と描画Transformの変更世代を個別に進める
		void MarkRenderDataModified();
		// 対象が分かる描画変更はEntity単位で記録する
		void MarkRenderDataModified(const Entity& entity);
		// 色だけの変更は描画構成と分離する
		void MarkMeshColorModified(const Entity& entity);
		uint64_t GetEntityRenderRevision(const Entity& entity) const;
		uint64_t GetMeshColorRevision(const Entity& entity) const;
		uint64_t GetMeshColorRevision() const { return meshColorRevision_; }
		uint64_t GetRenderResetRevision() const { return renderResetRevision_; }
		void MarkTransformConsumersModified(
			ComponentChangeChannel channels,
			std::span<const Entity> changedTransforms);

		// Component変更通知の購読を追加、削除する
		uint64_t AddComponentMutationListener(ComponentMutationCallback callback, void* userData);
		void RemoveComponentMutationListener(uint64_t listenerID);

		//============================================================================
		//	ヘルパー
		//============================================================================
		// シグネチャにマッチするエンティティ全てに対して関数を呼び出す
		template <typename... T, typename Fn>
		void ForEach(Fn&& fn);
		// DynamicBufferを持つエンティティを走査する
		template <typename T, typename Fn>
		void ForEachBuffer(Fn&& fn);
		// 固定ComponentType IDに一致するエンティティをアーキタイプ単位で走査
		template <typename Fn>
		void ForEach(uint32_t typeID, Fn&& fn);
		template <typename Fn>
		void ForEachAliveEntity(Fn&& fn);

		//--------- accessor -----------------------------------------------------

		// エンティティが存在するか
		bool IsAlive(const Entity& entity) const;
		// フレーム終端で破棄される予定か
		bool IsPendingDestroy(const Entity& entity) const;
		// エンティティのUUIDを返す
		UUID GetUUID(const Entity& entity) const;
		// UUIDからエンティティを検索する
		Entity FindByUUID(UUID id) const;

		// コンポーネントを持っているか
		template <typename T>
		bool HasComponent(const Entity& entity) const;
		bool HasComponent(const Entity& entity, uint32_t typeID) const;
		bool HasComponent(const Entity& entity, const std::string_view& typeName) const;
		// エンティティのコンポーネントを返す
		template <typename T>
		T& GetComponent(const Entity& entity);
		// コンポーネントがあればポインタを返す
		template <typename T>
		T* TryGetComponent(const Entity& entity);
		template <typename T>
		const T* TryGetComponent(const Entity& entity) const;
		// DynamicBufferを持っているか
		template <typename T>
		bool HasBuffer(const Entity& entity) const;
		// DynamicBufferを返す
		template <typename T>
		DynamicBuffer<T> GetBuffer(const Entity& entity);
		template <typename T>
		DynamicBuffer<T> TryGetBuffer(const Entity& entity);
		template <typename T>
		std::span<const T> GetBufferSpan(const Entity& entity) const;
		// 実行時ComponentType IDからPOD Bufferを操作する
		UntypedDynamicBuffer TryGetUntypedBuffer(
			const Entity& entity, uint32_t typeID);
		UntypedDynamicBuffer TryGetUntypedBuffer(
			const Entity& entity, uint32_t typeID) const;
		// Enableable Componentの有効状態
		template <typename T>
		void SetComponentEnabled(const Entity& entity, bool enabled);
		template <typename T>
		bool IsComponentEnabled(const Entity& entity) const;

		ECSWorldKind GetKind() const { return kind_; }
		// 構造またはComponent値が変わるたびに進む世代
		uint64_t GetDataRevision() const { return dataRevision_; }
		// 描画構成、描画Transform、ライト抽出の変更世代
		uint64_t GetRenderDataRevision() const { return renderDataRevision_; }
		uint64_t GetRenderTransformRevision() const { return renderTransformRevision_; }
		uint64_t GetLightDataRevision() const { return lightDataRevision_; }
		// 指定世代より後に描画へ影響したTransform一覧を取得
		bool CollectRenderTransformChanges(
			uint64_t afterRevision, std::vector<Entity>& outEntities) const;
		// EntityのTransformが影響する抽出先を返す
		ComponentChangeChannel GetTransformChangeChannels(
			const Entity& entity) const;
		// 現在レコードされているエンティティの数を返す
		uint32_t GetRecordCount() const { return static_cast<uint32_t>(records_.size()); }
		// 現在のArchetype数を返す、ForEachが走査するArchetypeの数
		uint32_t GetArchetypeCount() const { return static_cast<uint32_t>(archetypes_.size()); }
		// ECSのメモリ使用量と構造変更量を返す
		ECSWorldStatistics GetStatistics() const;
		// フレーム単位の構造変更統計をリセット
		void ResetFrameStatistics();
		// チャンク外データの所有先
		ECSStorageRegistry& GetStorage() { return storage_; }
		const ECSStorageRegistry& GetStorage() const { return storage_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// エンティティIDからエンティティの位置や世代を管理する配列
		std::vector<EntityRecord> records_;
		// 編集用または実行用ワールドの種別
		ECSWorldKind kind_ = ECSWorldKind::Authoring;
		// 破棄されたエンティティIDの再利用のための空きIDのスタック
		std::vector<uint32_t> free_;
		// フレーム終端でまとめて破棄するエンティティ
		std::vector<Entity> pendingDestroyEntities_;
		// スクリプト由来の構造変更を遅延適用するコマンドバッファでworld破棄時に未処理分は安全に破棄される
		WorldCommandBuffer commandBuffer_;
		// Prefab/SceneコマンドがFlushで参照する外部サービスで非所有ポインタ、EngineApplicationが設定する
		WorldCommandServices commandServices_{};
		// シーン側の永続UUIDからエンティティIDへのマップ
		std::unordered_map<UUID, Entity> uuidToEntity_;
		// ワールドに属するチャンク外データ
		ECSStorageRegistry storage_;

		// シグネチャからArchetypeへのマップ
		std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash> archetypes_;

		// 空のArchetypeでコンポーネントを持たないエンティティはここにまとめる
		EntityArchetype* emptyArchetype_ = nullptr;

		// クエリsignatureごとにマッチするarchetypeをキャッシュする計画
		struct ArchetypeMatchPlan {

			std::vector<EntityArchetype*> archetypes;
			uint32_t builtArchetypeVersion = 0xFFFFFFFFu;
		};
		// クエリsignatureからマッチするarchetype一覧を引くキャッシュ
		std::unordered_map<EntitySignature, ArchetypeMatchPlan, EntitySignatureHash> matchPlans_;
		// archetypeが増えるたびに進むversionでmatchPlans_の無効化に使う
		uint32_t archetypeVersion_ = 0;

		// Component変更通知の購読情報
		struct ComponentMutationListener {

			uint64_t id = 0;
			ComponentMutationCallback callback = nullptr;
			void* userData = nullptr;
		};
		// 描画Transformの世代ごとの差分
		struct RenderTransformChangeBatch {

			uint64_t revision = 0;
			std::vector<Entity> entities;
		};
		std::vector<ComponentMutationListener> componentMutationListeners_;
		std::deque<RenderTransformChangeBatch>
			renderTransformChangeHistory_;
		uint64_t nextComponentMutationListenerID_ = 1;
		static constexpr size_t kRenderTransformHistoryCount = 8;
		// 0を未構築値として扱えるよう1から開始する
		uint64_t dataRevision_ = 1;
		uint64_t renderDataRevision_ = 1;
		uint64_t renderResetRevision_ = 1;
		uint64_t meshColorRevision_ = 1;
		// Entityの世代を含むキーで再利用後の変更を区別する
		std::unordered_map<uint64_t, uint64_t> entityRenderRevisions_;
		std::unordered_map<uint64_t, uint64_t> meshColorRevisions_;
		uint64_t renderTransformRevision_ = 1;
		uint64_t lightDataRevision_ = 1;
		// フレーム内の構造変更統計
		uint64_t structuralMigrationCount_ = 0;
		uint64_t relocatedComponentCount_ = 0;
		uint64_t relocatedComponentBytes_ = 0;

		//--------- functions ----------------------------------------------------

		// 新しいエンティティIDを割り当てる
		uint32_t AllocateIndex();
		// 指定アーキタイプへエンティティを作成する本体
		Entity CreateEntityInArchetype(EntityArchetype* archetype, UUID stableUUID);
		// エンティティが存在することを確認する、存在しない場合はアサート
		void AssertAlive(const Entity& entity) const;
		// エンティティを即時破棄する本体でFlushPendingDestroyEntitiesからのみ呼び出す
		void DestroyEntityImmediate(const Entity& entity);

		// エンティティが所属するArchetypeを移動する本体
		void MigrateEntity(const Entity& entity, const EntitySignature& oldSignature, const EntitySignature& newSignature);
		// シグネチャにマッチするArchetypeがあれば返し、なければ作成して返す
		EntityArchetype* GetOrCreateArchetype(const EntitySignature& signature);
		// Component変更を購読側へ通知する
		void NotifyComponentMutation(const Entity& entity, uint32_t typeID, ComponentMutationKind kind);
		// Entityが持つComponentの値変更先をまとめる
		ComponentChangeChannel GetChangeChannels(
			const Entity& entity) const;
		// 0を飛ばして変更世代を進める
		static void IncrementRevision(uint64_t& revision);
		// 指定エンティティから外れるチャンク外データを解放する
		void ReleaseExternalComponents(const Entity& entity, const EntitySignature* retainedSignature);
		// コンポーネントをこのワールドへ格納できるか
		bool CanStoreComponent(const ComponentTypeInfo& info) const;

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

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はAddBufferを使用してください");
		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag,
			"Tagは値を持たないAPIを使用してください");

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		Assert::Call(CanStoreComponent(
			ComponentTypeRegistry::GetInstance().GetInfo(typeID)),
			"ComponentTypeをこのWorldへ格納できません");

		// 新しいシグネチャを作るために古いシグネチャを取ってくる
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		// 既に持ってるなら参照をそのまま返す
		if (oldSignature.Test(typeID)) {

			auto& location = records_[entity.index].location;
			void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
			return *(T*)ptr;
		}

		EntitySignature newSignature = oldSignature;
		newSignature.Set(typeID);
		// シグネチャを更新してArchetypeを移動する
		MigrateEntity(entity, oldSignature, newSignature);

		// 関連Buffer等を最終構築した後に現在位置から参照を引き直す
		auto& addedLocation = records_[entity.index].location;
		void* addedPtr = addedLocation.archetype->GetRaw(
			addedLocation.chunkIndex, addedLocation.row, typeID);
		ComponentTypeRegistry::GetInstance().GetInfo(typeID).onAdded(
			*this, entity, addedPtr);
		auto& location = records_[entity.index].location;
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
		NotifyComponentMutation(entity, typeID, ComponentMutationKind::Added);
		return *(T*)ptr;
	}

	template <typename T>
	inline void ECSWorld::RemoveComponent(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はRemoveBufferを使用してください");

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();

		// 新しいシグネチャを作るために古いシグネチャを取ってくる
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		if (!oldSignature.Test(typeID)) {
			return;
		}

		EntitySignature newSignature = oldSignature;
		newSignature.Reset(typeID);

		// シグネチャを更新してArchetypeを移動する
		MigrateEntity(entity, oldSignature, newSignature);
		// 本体削除後に関連BufferやRuntime Componentを連動して外す
		ComponentTypeRegistry::GetInstance().GetInfo(typeID).onRemoved(
			*this, entity);
		NotifyComponentMutation(entity, typeID, ComponentMutationKind::Removed);
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::AddBuffer(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		Assert::Call(CanStoreComponent(
			ComponentTypeRegistry::GetInstance().GetInfo(typeID)),
			"ComponentTypeをこのWorldへ格納できません");
		EntitySignature oldSignature =
			records_[entity.index].location.archetype->GetSignature();
		if (!oldSignature.Test(typeID)) {

			EntitySignature newSignature = oldSignature;
			newSignature.Set(typeID);
			MigrateEntity(entity, oldSignature, newSignature);
			NotifyComponentMutation(entity, typeID, ComponentMutationKind::Added);
		}
		return GetBuffer<T>(entity);
	}

	template <typename T>
	inline void ECSWorld::RemoveBuffer(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		EntitySignature oldSignature =
			records_[entity.index].location.archetype->GetSignature();
		if (!oldSignature.Test(typeID)) {
			return;
		}

		EntitySignature newSignature = oldSignature;
		newSignature.Reset(typeID);
		MigrateEntity(entity, oldSignature, newSignature);
		NotifyComponentMutation(entity, typeID, ComponentMutationKind::Removed);
	}

	template <typename T>
	inline void ECSWorld::MarkComponentModified(const Entity& entity) {

		if (!IsAlive(entity)) {
			return;
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		MarkComponentModified(entity, typeID);
	}

	template <typename ...T, typename Fn>
	inline void ECSWorld::ForEach(Fn&& fn) {

		static_assert(((ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer) && ...),
			"DynamicBufferはForEachBufferを使用してください");
		static_assert(((ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag) && ...),
			"Tagを値として取得できません");

		// 必要な型IDは最初に一度だけ取得する
		ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
		const std::array<uint32_t, sizeof...(T)> requiredTypeIDs = { registry.GetID<T>()... };

		EntitySignature required{};
		for (uint32_t typeID : requiredTypeIDs) {
			required.Set(typeID);
		}

		// signatureにマッチするarchetype一覧をキャッシュし、毎回の全archetype走査を避ける
		ArchetypeMatchPlan& plan = matchPlans_[required];
		if (plan.builtArchetypeVersion != archetypeVersion_) {

			// archetypeが増えた時だけ作り直す、archetypeは破棄されないのでpointerは有効なまま
			plan.archetypes.clear();
			for (auto& [signature, archPtr] : archetypes_) {

				EntityArchetype* archetype = archPtr.get();
				// シグネチャが必要なコンポーネントを全て含んでいるか
				if (archetype->GetSignature().Contains(required)) {
					plan.archetypes.emplace_back(archetype);
				}
			}
			plan.builtArchetypeVersion = archetypeVersion_;
		}

		// 関数を同一実体のまま全チャンクで再利用する
		Fn& fnRef = fn;
		for (EntityArchetype* archetype : plan.archetypes) {

			// このArchetypeに対する列番号を一度だけ解決する
			const std::array<uint32_t, sizeof...(T)> columnIndices = ResolveColumnIndices(
				archetype, requiredTypeIDs, std::index_sequence_for<T...>{});

			// チャンクを走査
			for (auto& chunk : archetype->GetChunks()) {

				if (chunk->GetCount() == 0) {
					continue;
				}
				// レコード再検索を挟まず直接列アクセスする
				ForEachChunkFast<T...>(*chunk, columnIndices, fnRef, std::index_sequence_for<T...>{});
			}
		}
	}

	template <typename T, typename Fn>
	inline void ECSWorld::ForEachBuffer(Fn&& fn) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		ForEach(typeID, [&](const Entity& entity) {
			if (!IsComponentEnabled<T>(entity)) {
				return;
			}
			fn(entity, GetBuffer<T>(entity));
			});
	}

	template <typename Fn>
	inline void ECSWorld::ForEach(uint32_t typeID, Fn&& fn) {

		if (ComponentTypeRegistry::GetInstance().GetComponentTypeCount() <= typeID) {
			return;
		}

		EntitySignature required{};
		required.Set(typeID);

		ArchetypeMatchPlan& plan = matchPlans_[required];
		if (plan.builtArchetypeVersion != archetypeVersion_) {

			plan.archetypes.clear();
			for (auto& [signature, archPtr] : archetypes_) {

				EntityArchetype* archetype = archPtr.get();
				if (archetype->GetSignature().Contains(required)) {
					plan.archetypes.emplace_back(archetype);
				}
			}
			plan.builtArchetypeVersion = archetypeVersion_;
		}

		Fn& fnRef = fn;
		for (EntityArchetype* archetype : plan.archetypes) {
			for (auto& chunk : archetype->GetChunks()) {

				const auto entities = chunk->GetEntities();
				const uint32_t count = chunk->GetCount();
				for (uint32_t row = 0; row < count; ++row) {
					fnRef(entities[row]);
				}
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

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はHasBufferを使用してください");

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

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はGetBufferを使用してください");
		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag,
			"Tagは値を持ちません");

		// エンティティが存在することを確認
		AssertAlive(entity);

		// 型をタイプIDに変換
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
		return *(T*)ptr;
	}

	template <typename T>
	inline T* ECSWorld::TryGetComponent(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はTryGetBufferを使用してください");

		// エンティティやコンポーネントが無い場合はnullptrを返す
		if (!IsAlive(entity)) {
			return nullptr;
		}
		uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		if (!location.archetype->Has(typeID)) {
			return nullptr;
		}
		return reinterpret_cast<T*>(location.archetype->GetRaw(location.chunkIndex, location.row, typeID));
	}

	template <typename T>
	inline const T* ECSWorld::TryGetComponent(const Entity& entity) const {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はGetBufferSpanを使用してください");
		if (!IsAlive(entity)) {
			return nullptr;
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		const auto& location = records_[entity.index].location;
		if (!location.archetype->Has(typeID)) {
			return nullptr;
		}
		return reinterpret_cast<const T*>(
			location.archetype->GetRaw(location.chunkIndex, location.row, typeID));
	}

	template <typename T>
	inline bool ECSWorld::HasBuffer(const Entity& entity) const {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		if (!IsAlive(entity)) {
			return false;
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		return records_[entity.index].location.archetype->Has(typeID);
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::GetBuffer(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		void* ptr = location.archetype->GetRaw(
			location.chunkIndex, location.row, typeID);
		return DynamicBuffer<T>(static_cast<DynamicBufferHeader*>(ptr));
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::TryGetBuffer(const Entity& entity) {

		return HasBuffer<T>(entity) ? GetBuffer<T>(entity) : DynamicBuffer<T>{};
	}

	template <typename T>
	inline std::span<const T> ECSWorld::GetBufferSpan(const Entity& entity) const {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		if (!IsAlive(entity)) {
			return {};
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		const auto& location = records_[entity.index].location;
		if (!location.archetype->Has(typeID)) {
			return {};
		}
		const auto* header = static_cast<const DynamicBufferHeader*>(
			location.archetype->GetRaw(
				location.chunkIndex, location.row, typeID));
		return { static_cast<const T*>(header->data), header->size };
	}

	template <typename T>
	inline void ECSWorld::SetComponentEnabled(const Entity& entity, bool enabled) {

		static_assert(ComponentTypeTraits::ResolveEnableable<T>(),
			"kEnableableがtrueのComponentだけ状態を変更できます");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		const uint32_t columnIndex = location.archetype->GetColumnIndex(typeID);
		EntityChunk* chunk = location.archetype->GetChunks()[location.chunkIndex].get();
		if (chunk->IsEnabledByColumnIndex(columnIndex, location.row) == enabled) {
			return;
		}
		chunk->SetEnabledByColumnIndex(columnIndex, location.row, enabled);
		NotifyComponentMutation(entity, typeID, ComponentMutationKind::Modified);
	}

	template <typename T>
	inline bool ECSWorld::IsComponentEnabled(const Entity& entity) const {

		if (!IsAlive(entity)) {
			return false;
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		const auto& location = records_[entity.index].location;
		if (!location.archetype->Has(typeID)) {
			return false;
		}
		const uint32_t columnIndex = location.archetype->GetColumnIndex(typeID);
		return location.archetype->GetChunks()[location.chunkIndex]->IsEnabledByColumnIndex(
			columnIndex, location.row);
	}

	template <size_t N, size_t... I>
	inline std::array<uint32_t, N> ECSWorld::ResolveColumnIndices(EntityArchetype* archetype,
		const std::array<uint32_t, N>& typeIDs, std::index_sequence<I...>) {

		return { archetype->GetColumnIndex(typeIDs[I])... };
	}

	template <typename... T, typename Fn, size_t... I>
	inline void ECSWorld::ForEachChunkFast(EntityChunk& chunk, const std::array<uint32_t, sizeof...(T)>& columnIndices,
		Fn& fn, std::index_sequence<I...>) {

		const auto entities = chunk.GetEntities();
		const uint32_t count = chunk.GetCount();
		const std::tuple<T*...> columns{
			reinterpret_cast<T*>(chunk.GetColumnDataByColumnIndex(columnIndices[I]))...
		};
		for (uint32_t row = 0; row < count; ++row) {

			if (!(chunk.IsEnabledByColumnIndex(columnIndices[I], row) && ...)) {
				continue;
			}
			// チャンクごとに一度解決した列先頭から行を取り出す
			fn(entities[row], (std::get<I>(columns)[row])...);
		}
	}
} // Engine

