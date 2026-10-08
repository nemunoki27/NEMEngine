#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Entity/EntityArchetype.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/World/WorldCommandBuffer.h>
#include <Engine/Core/World/ECS/World/ECSQueryCache.h>
#include <Engine/Core/World/ECS/World/ECSChangeTracker.h>
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <deque>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

	//============================================================================
	//	ECSWorldKind enum
	//	編集用と実行用のWorldを区別する
	//============================================================================
	enum class ECSWorldKind : uint8_t {

		Authoring,
		Runtime,
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
		bool destroying = false;
		uint32_t nextFree = UINT32_MAX;

		// 保存と複製で引き継ぐUUID
		UUID uuid{};

		// 格納先のアーキタイプと行位置
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

	// 無効Componentを走査へ含めるか
	enum class ECSQueryMode : uint8_t {

		EnabledOnly,
		IncludeDisabled,
	};

	//============================================================================
	//	ECSWorld class
	//	シーンを構成するエンティティとコンポーネントを管理するクラス
	//============================================================================
	class ECSWorldSerialization;

	class ECSWorld {
		friend class ECSWorldSerialization;
		friend class WorldCommandBuffer;
		friend class ECSCreationScope;
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
		// スクリプトの構造変更を安全地点まで保持する
		WorldCommandBuffer& GetCommandBuffer() { return commandBuffer_; }
		// 積まれた構造変更コマンドをまとめて適用する
		void FlushWorldCommands() { commandBuffer_.Flush(*this); }

		// Scene操作で借用するサービスを接続する
		void SetCommandServices(const WorldCommandServices& services);
		const WorldCommandServices& GetCommandServices() const { return commandServices_; }
		// 接続の変更番号を取得する
		uint64_t GetCommandServiceRevision() const { return commandServiceRevision_; }

		//============================================================================
		//	コンポーネントに対して行う操作
		//============================================================================
		using ComponentMutationCallback = ECSChangeTracker::ComponentMutationCallback;

		// エンティティにコンポーネントを追加
		template <typename T>
		T& AddComponent(const Entity& entity);
		bool AddComponentByName(const Entity& entity, std::string_view typeName);
		// エンティティからコンポーネントを削除
		template <typename T>
		void RemoveComponent(const Entity& entity);
		bool RemoveComponentByName(const Entity& entity, std::string_view typeName);
		// DynamicBufferを追加する
		template <typename T>
		DynamicBuffer<T> AddBuffer(const Entity& entity);
		// Bufferを置き換えて変更を通知する
		template <typename T>
		void SetBuffer(const Entity& entity, std::span<const T> values);
		// DynamicBufferを削除する
		template <typename T>
		void RemoveBuffer(const Entity& entity);

		// JSONからComponentを追加する
		bool AddComponentFromJson(const Entity& entity, std::string_view typeName, const nlohmann::json& data);
		// 追加済みComponentへJSONを適用する
		bool ApplyComponentJson(const Entity& entity, std::string_view typeName, const nlohmann::json& data);
		// 保存対象を同じEntity IDの独立ワールドへ複製する
		std::unique_ptr<ECSWorld> CloneForSerialization() const;
		// EntityのComponentをJSONへ変換する
		void SerializeEntityComponents(const Entity& entity, nlohmann::json& outComponents) const;
		bool SerializeComponentToJson(const Entity& entity, std::string_view typeName, nlohmann::json& outData) const;

		// 構造を変えない値変更を購読側へ通知する
		template <typename T>
		void MarkComponentModified(const Entity& entity);
		void MarkComponentModified(const Entity& entity, uint32_t typeID);
		// 複数の値変更をまとめて描画抽出へ通知する
		void MarkDataModified();
		// 描画構成と描画座標の変更世代を個別に進める
		void MarkRenderDataModified();
		// 対象が分かる描画変更をエンティティ単位で記録する
		void MarkRenderDataModified(const Entity& entity);
		// 色だけの変更は描画構成と分離する
		void MarkMeshColorModified(const Entity& entity);
		uint64_t GetEntityRenderRevision(const Entity& entity) const;
		uint64_t GetMeshColorRevision(const Entity& entity) const;
		uint64_t GetMeshColorRevision() const { return changes_.GetMeshColorRevision(); }
		uint64_t GetRenderResetRevision() const { return changes_.GetRenderResetRevision(); }
		void MarkTransformConsumersModified(ComponentChangeChannel channels, std::span<const Entity> changedTransforms);

		// 値変更通知の購読を追加と削除する
		uint64_t AddComponentMutationListener(ComponentMutationCallback callback, void* userData);
		void RemoveComponentMutationListener(uint64_t listenerID);

		//============================================================================
		//	ヘルパー
		//============================================================================
		// シグネチャにマッチするエンティティ全てに対して関数を呼び出す
		template <typename... T, typename Fn>
		void ForEach(Fn&& fn);
		// 読み取り専用WorldからComponentを変更せず走査する
		template <typename... T, typename Fn>
		void ForEach(Fn&& fn) const;
		// 値の編集と通知が終わるまで構造変更を予約へ回す
		template <typename Fn>
		void WithStableStructure(Fn&& action);
		// 可変長バッファを持つエンティティを走査する
		template <typename T, typename Fn>
		void ForEachBuffer(Fn&& fn);
		// 指定の型IDを持つエンティティを走査する
		template <typename Fn>
		void ForEach(uint32_t typeID, Fn&& fn, ECSQueryMode mode = ECSQueryMode::EnabledOnly);
		template <typename Fn>
		void ForEachAliveEntity(Fn&& fn) const;

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
		bool HasComponent(const Entity& entity, std::string_view typeName) const;
		// エンティティのコンポーネントを返す
		template <typename T>
		T& GetComponent(const Entity& entity);
		template <typename T>
		const T& GetComponent(const Entity& entity) const;
		// コンポーネントがあればポインタを返す
		template <typename T>
		T* TryGetComponent(const Entity& entity);
		template <typename T>
		const T* TryGetComponent(const Entity& entity) const;
		// 可変長バッファを持っているか
		template <typename T>
		bool HasBuffer(const Entity& entity) const;
		// 可変長バッファを返す
		template <typename T>
		DynamicBuffer<T> GetBuffer(const Entity& entity);
		template <typename T>
		DynamicBuffer<const T> GetBuffer(const Entity& entity) const;
		template <typename T>
		DynamicBuffer<T> TryGetBuffer(const Entity& entity);
		template <typename T>
		DynamicBuffer<const T> TryGetBuffer(const Entity& entity) const;
		template <typename T>
		std::span<const T> GetBufferSpan(const Entity& entity) const;
		// 実行時の型IDからPODバッファを操作する
		UntypedDynamicBuffer TryGetUntypedBuffer(const Entity& entity, uint32_t typeID);
		ReadOnlyUntypedDynamicBuffer TryGetUntypedBuffer(const Entity& entity, uint32_t typeID) const;
		// 個別に切り替えられる有効状態
		template <typename T>
		void SetComponentEnabled(const Entity& entity, bool enabled);
		template <typename T>
		bool IsComponentEnabled(const Entity& entity) const;

		// Componentの削除再追加を識別する番号を返す
		uint64_t GetComponentInstanceID(const Entity& entity, uint32_t typeID) const;
		// 追加待ちも含めたScript用の個体番号を返す
		uint64_t GetBindingComponentInstanceID(const Entity& entity, uint32_t typeID) const;
		// 追加前の設定値を保持したままChunkへ反映する
		void ApplyPendingComponent(const Entity& entity, const PendingComponent& component);
		// Scriptからは追加待ちの値も読み書きする
		template <typename T>
		T* TryGetComponentForBinding(const Entity& entity);
		template <typename T>
		const T* TryGetComponentForBinding(const Entity& entity) const;
		// 保存処理も追加待ちBufferを読み取り専用で参照する
		template <typename T>
		DynamicBuffer<const T> TryGetBufferForBinding(const Entity& entity) const;
		template <typename T>
		DynamicBuffer<T> TryGetBufferForBinding(const Entity& entity);
		UntypedDynamicBuffer TryGetBufferForBinding(const Entity& entity, uint32_t typeID);
		ReadOnlyUntypedDynamicBuffer TryGetBufferForBinding(const Entity& entity, uint32_t typeID) const;

		// 走査または構造変更が終わるまでCommandを保留する
		bool IsStructuralChangeDeferred() const { return queryDepth_ != 0 || structuralChange_; }
		ECSWorldKind GetKind() const { return kind_; }
		// Worldを所有せず終了状態だけを保持する
		std::shared_ptr<const ECSWorldLifetime> GetLifetime() const { return lifetime_; }
		// 構造または値が変わるたびに進む世代
		uint64_t GetDataRevision() const { return changes_.GetDataRevision(); }
		// 描画構成と座標、照明抽出の変更世代
		uint64_t GetRenderDataRevision() const { return changes_.GetRenderDataRevision(); }
		uint64_t GetRenderTransformRevision() const { return changes_.GetRenderTransformRevision(); }
		uint64_t GetLightDataRevision() const { return changes_.GetLightDataRevision(); }
		// 指定世代より後に描画へ影響した座標を取得する
		bool CollectRenderTransformChanges(uint64_t afterRevision, std::vector<Entity>& outEntities) const;
		// エンティティの座標が影響する抽出先を返す
		ComponentChangeChannel GetTransformChangeChannels(const Entity& entity) const;
		// 現在レコードされているエンティティの数を返す
		uint32_t GetRecordCount() const { return static_cast<uint32_t>(records_.size()); }
		// 現在のアーキタイプ数を返す
		uint32_t GetArchetypeCount() const { return static_cast<uint32_t>(storageState_->archetypes.size()); }
		// ECSのメモリ使用量と構造変更量を返す
		ECSWorldStatistics GetStatistics() const;
		// フレーム単位の構造変更統計をリセット
		void ResetFrameStatistics();
		// チャンク外データの所有先
		ECSStorageRegistry& GetStorage() { return storageState_->storage; }
		const ECSStorageRegistry& GetStorage() const { return storageState_->storage; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 操作中にWorldが終了しても構築先を保持する
		struct StorageState {

			// Chunk破棄後に外部データを解放する
			ECSStorageRegistry storage;
			// シグネチャごとのChunk所有先
			std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash> archetypes;
		};

		// 構造変更の終了まで格納先を保持する
		class StructuralScope {
		public:
			//====================================================================
			//	public Methods
			//====================================================================

			explicit StructuralScope(ECSWorld& world);
			~StructuralScope();
			StructuralScope(const StructuralScope&) = delete;
			StructuralScope& operator=(const StructuralScope&) = delete;

		private:
			//====================================================================
			//	private Methods
			//====================================================================

			//--------- variables ------------------------------------------------

			ECSWorld& world_;
			// 構造変更中のWorld終了を確認する
			std::shared_ptr<const ECSWorldLifetime> lifetime_;
			std::shared_ptr<StorageState> storageState_;
		};

		// 走査の終了まで格納先を保持する
		class QueryScope {
		public:
			//====================================================================
			//	public Methods
			//====================================================================

			explicit QueryScope(const ECSWorld& world);
			~QueryScope();
			QueryScope(const QueryScope&) = delete;
			QueryScope& operator=(const QueryScope&) = delete;

			//--------- accessor -------------------------------------------------

			// 走査元のWorldが終了していないか
			bool IsWorldAlive() const { return lifetime_->IsAlive(); }
		private:
			//====================================================================
			//	private Methods
			//====================================================================

			//--------- variables ------------------------------------------------

			const ECSWorld& world_;
			std::shared_ptr<const ECSWorldLifetime> lifetime_;
			std::shared_ptr<StorageState> storageState_;
		};

		//--------- variables ----------------------------------------------------

		// 構造変更中の再入を検出する
		bool structuralChange_ = false;
		// Chunkを借用している走査の深度
		mutable size_t queryDepth_ = 0;
		// 新しいComponentへ割り当てる個体番号
		uint64_t nextComponentInstanceID_ = 1;
		// 登録先へWorld終了を伝える状態
		std::shared_ptr<ECSWorldLifetime> lifetime_ = std::make_shared<ECSWorldLifetime>();
		// エンティティIDからエンティティの位置や世代を管理する配列
		std::deque<EntityRecord> records_;
		// 編集用または実行用ワールドの種別
		ECSWorldKind kind_ = ECSWorldKind::Authoring;
		// 再利用するEntity枠の先頭
		uint32_t freeHead_ = UINT32_MAX;
		// フレーム終端でまとめて破棄するエンティティ
		std::deque<Entity> pendingDestroyEntities_;
		// 破棄通知からのFlush再入を防ぐ
		bool flushingDestroy_ = false;
		// 安全地点まで構造変更を保持する
		WorldCommandBuffer commandBuffer_;
		// Scene操作で借用する外部サービス
		WorldCommandServices commandServices_{};
		uint64_t commandServiceRevision_ = 0; // Scene操作の接続変更
		// 保存と複製で引き継ぐUUIDからEntityを検索する
		std::unordered_map<UUID, Entity> uuidToEntity_;
		// Chunkと外部データを操作終了まで保持する
		std::shared_ptr<StorageState> storageState_ = std::make_shared<StorageState>();

		// 値を持たないエンティティを空のアーキタイプへまとめる
		EntityArchetype* emptyArchetype_ = nullptr;

		// クエリに一致するアーキタイプの検索計画
		mutable ECSQueryCache queryCache_;
		// アーキタイプ追加時の検索計画更新に使う世代
		uint32_t archetypeVersion_ = 0;

		// 変更世代と購読通知の状態
		ECSChangeTracker changes_;
		// フレーム内の構造変更統計
		uint64_t structuralMigrationCount_ = 0;
		uint64_t relocatedComponentCount_ = 0;
		uint64_t relocatedComponentBytes_ = 0;

		//--------- functions ----------------------------------------------------

		// Entityが持つBufferの型情報を検証して返す
		const ComponentTypeInfo* FindBufferType(const Entity& entity, uint32_t typeID) const;

		// 新しいエンティティIDを割り当てる
		uint32_t AllocateIndex();
		// Entity枠を無効化し世代を進める
		void RecycleIndex(uint32_t index);
		// 連続したComponent個体番号を予約する
		uint64_t ReserveComponentInstanceIDs(size_t count);
		// 指定アーキタイプへエンティティを作成する本体
		Entity CreateEntityInArchetype(EntityArchetype& archetype, UUID stableUUID);
		// エンティティが存在することを確認する、存在しない場合はアサート
		void AssertAlive(const Entity& entity) const;
		// 安全地点の削除と生成取消でEntityを即時破棄する
		void DestroyEntityImmediate(const Entity& entity);

		// エンティティの所属アーキタイプを移動する
		void MigrateEntity(const Entity& entity, const EntitySignature& oldSignature,
			const EntitySignature& newSignature, const PendingComponent* pending = nullptr);
		// シグネチャに合うアーキタイプを取得または作成する
		EntityArchetype& GetOrCreateArchetype(const EntitySignature& signature);
		// 関連Component処理の成否にかかわらず変更を通知する
		void CompleteComponentChange(const Entity& entity, uint32_t typeID, ComponentMutationKind kind);
		// Component変更を購読側へ通知する
		void NotifyComponentMutation(const Entity& entity, uint32_t typeID, ComponentMutationKind kind);
		// Entityが持つComponentの値変更先をまとめる
		ComponentChangeChannel GetChangeChannels(const Entity& entity) const;
		// 指定エンティティから外れるチャンク外データを解放する
		void ReleaseExternalComponents(const Entity& entity, const EntitySignature* retainedSignature);
		// コンポーネントをこのワールドへ格納できるか
		bool CanStoreComponent(const ComponentTypeInfo& info) const;
		// Worldの格納条件に合わないComponentを拒否する
		void ValidateComponentStorage(const ComponentTypeInfo& info) const;
		// 有効なEntityの追加予約から値を取得する
		void* TryGetPendingComponentData(const Entity& entity, uint32_t typeID);
		const void* TryGetPendingComponentData(const Entity& entity, uint32_t typeID) const;

		// Worldの読取条件を保ってComponentを取得する
		template <typename T, typename World>
		static decltype(auto) GetComponentValue(World& world, const Entity& entity);
		// Worldの読取条件を保って共通のQuery計画を走査する
		template <typename... T, typename World, typename Fn>
		static void ForEachComponents(World& world, Fn&& fn);
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
		ValidateComponentStorage(ComponentTypeRegistry::GetInstance().GetInfo(typeID));

		// 新しいシグネチャを作るために古いシグネチャを取ってくる
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		// 既に持ってるなら参照をそのまま返す
		if (oldSignature.Test(typeID)) {

			auto& location = records_[entity.index].location;
			void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
			return *static_cast<T*>(ptr);
		}

		EntitySignature newSignature = oldSignature;
		newSignature.Set(typeID);
		// シグネチャを更新して所属アーキタイプを移動する
		MigrateEntity(entity, oldSignature, newSignature);

		// 通知中の構造移動後も同じ個体だけを返す
		const uint64_t instanceID = GetComponentInstanceID(entity, typeID);
		CompleteComponentChange(entity, typeID, ComponentMutationKind::Added);
		T* result = TryGetComponent<T>(entity);
		if (!result || GetComponentInstanceID(entity, typeID) != instanceID) {
			throw std::runtime_error("追加通知中にComponentが削除されました");
		}
		return *result;
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

		// シグネチャを更新して所属アーキタイプを移動する
		MigrateEntity(entity, oldSignature, newSignature);
		// 本体削除後に関連バッファと実行用の値を外す
		CompleteComponentChange(entity, typeID, ComponentMutationKind::Removed);
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::AddBuffer(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		ValidateComponentStorage(ComponentTypeRegistry::GetInstance().GetInfo(typeID));
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
		if (!oldSignature.Test(typeID)) {

			EntitySignature newSignature = oldSignature;
			newSignature.Set(typeID);
			MigrateEntity(entity, oldSignature, newSignature);
			// 通知で置き換わった別のBufferを追加結果にしない
			const uint64_t instanceID = GetComponentInstanceID(entity, typeID);
			NotifyComponentMutation(entity, typeID, ComponentMutationKind::Added);
			if (!HasBuffer<T>(entity) || GetComponentInstanceID(entity, typeID) != instanceID) {
				throw std::runtime_error("追加通知中にBufferが削除されました");
			}
		}
		return GetBuffer<T>(entity);
	}

	template <typename T>
	inline void ECSWorld::SetBuffer(const Entity& entity, std::span<const T> values) {

		if (values.size() > UINT32_MAX) throw std::length_error("DynamicBufferの要素数が上限を超えています");
		if (auto buffer = TryGetBuffer<T>(entity); buffer.IsValid()) {
			buffer.Assign(values);
		} else {
			// 構造変更で同じChunkの入力が移動する前に複製する
			const std::vector<T> copied(values.begin(), values.end());
			AddBuffer<T>(entity).Assign(copied);
		}
		MarkComponentModified<T>(entity);
	}

	template <typename T>
	inline void ECSWorld::RemoveBuffer(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		EntitySignature oldSignature = records_[entity.index].location.archetype->GetSignature();
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

	template <typename Fn>
	inline void ECSWorld::WithStableStructure(Fn&& action) {

		const auto lifetime = GetLifetime();
		QueryScope query(*this);
		action();
		// 通知中に終了したWorldへ戻らない
		if (!lifetime->IsAlive()) {
			throw std::runtime_error("処理中にWorldが終了しました");
		}
	}

	template <typename... T, typename Fn>
	inline void ECSWorld::ForEach(Fn&& fn) {

		ForEachComponents<T...>(*this, std::forward<Fn>(fn));
	}

	template <typename... T, typename Fn>
	inline void ECSWorld::ForEach(Fn&& fn) const {

		ForEachComponents<T...>(*this, std::forward<Fn>(fn));
	}

	template <typename... T, typename World, typename Fn>
	inline void ECSWorld::ForEachComponents(World& world, Fn&& fn) {

		static_assert(((ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer) && ...),
			"DynamicBufferはForEachBufferを使用してください");
		static_assert(((ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag) && ...),
			"Tagを値として取得できません");

		// 必要な型IDは最初に一度だけ取得する
		QueryScope query(world);
		ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
		const std::array<uint32_t, sizeof...(T)> requiredTypeIDs = {registry.GetID<T>()...};

		EntitySignature required{};
		for (uint32_t typeID : requiredTypeIDs) {
			required.Set(typeID);
		}

		// 条件に合うアーキタイプの一覧を再利用する
		const auto& matchingArchetypes =
			world.queryCache_.Resolve(required, world.storageState_->archetypes, world.archetypeVersion_);

		// 関数を同一実体のまま全チャンクで再利用する
		Fn& fnRef = fn;
		for (EntityArchetype* archetype : matchingArchetypes) {

			// アーキタイプごとに列番号を一度だけ解決する
			const std::array<uint32_t, sizeof...(T)> columnIndices =
				ECSQueryCache::ResolveColumnIndices(*archetype, requiredTypeIDs, std::index_sequence_for<T...>{});

			// チャンクを走査
			std::conditional_t<std::is_const_v<World>, const EntityArchetype, EntityArchetype>& selectedArchetype = *archetype;
			for (uint32_t index = 0; index < selectedArchetype.GetChunkCount(); ++index) {

				auto& chunk = selectedArchetype.GetChunk(index);
				if (chunk.GetCount() == 0) {
					continue;
				}
				// レコード再検索を挟まず直接列アクセスする
				ECSQueryCache::ForEachChunkFast<T...>(chunk, columnIndices, fnRef, query, std::index_sequence_for<T...>{});
			}
		}
	}

	template <typename T, typename Fn>
	inline void ECSWorld::ForEachBuffer(Fn&& fn) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		ForEach(typeID, [&](const Entity& entity) { fn(entity, GetBuffer<T>(entity)); });
	}

	template <typename Fn>
	inline void ECSWorld::ForEach(uint32_t typeID, Fn&& fn, ECSQueryMode mode) {

		QueryScope query(*this);
		if (ComponentTypeRegistry::GetInstance().GetComponentTypeCount() <= typeID) {
			return;
		}

		EntitySignature required{};
		required.Set(typeID);

		const auto& matchingArchetypes = queryCache_.Resolve(required, storageState_->archetypes, archetypeVersion_);

		Fn& fnRef = fn;
		for (EntityArchetype* archetype : matchingArchetypes) {
			for (uint32_t index = 0; index < archetype->GetChunkCount(); ++index) {

				const auto& chunk = archetype->GetChunk(index);
				const auto entities = chunk.GetEntities();
				const uint32_t count = chunk.GetCount();
				const uint32_t column = archetype->GetColumnIndex(typeID);
				for (uint32_t row = 0; row < count; ++row) {
					if (mode == ECSQueryMode::EnabledOnly && !chunk.IsEnabledByColumnIndex(column, row)) {
						continue;
					}
					fnRef(entities[row]);
					if (!query.IsWorldAlive()) {
						throw std::runtime_error("走査中にWorldが終了しました");
					}
				}
			}
		}
	}

	template <typename Fn>
	inline void ECSWorld::ForEachAliveEntity(Fn&& fn) const {

		const auto lifetime = GetLifetime();
		if (structuralChange_) {
			throw std::logic_error("構造変更途中のEntityを走査できません");
		}
		// Entity一覧だけを保持し、呼出し中の構造変更を許可する
		std::vector<Entity> entities;
		entities.reserve(records_.size());
		for (uint32_t index = 0; index < records_.size(); ++index) {
			if (records_[index].alive) {
				entities.push_back({index, records_[index].generation});
			}
		}
		for (const Entity& entity : entities) {
			if (IsAlive(entity)) {
				fn(entity);
				if (!lifetime->IsAlive()) {
					throw std::runtime_error("走査中にWorldが終了しました");
				}
			}
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

		return GetComponentValue<T>(*this, entity);
	}

	template <typename T>
	inline const T& ECSWorld::GetComponent(const Entity& entity) const {

		return GetComponentValue<T>(*this, entity);
	}

	template <typename T, typename World>
	inline decltype(auto) ECSWorld::GetComponentValue(World& world, const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag,
			"Tagは値を持たないAPIを使用してください");
		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Buffer,
			"DynamicBuffer要素はGetBufferを使用してください");

		// EntityとComponentの列を確認して取得する
		world.AssertAlive(entity);
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		const auto& location = world.records_[entity.index].location;
		std::conditional_t<std::is_const_v<World>, const EntityArchetype, EntityArchetype>& archetype = *location.archetype;
		return *reinterpret_cast<std::conditional_t<std::is_const_v<World>, const T*, T*>>(
			archetype.GetRaw(location.chunkIndex, location.row, typeID));
	}

	template <typename T>
	inline T* ECSWorld::TryGetComponent(const Entity& entity) {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag,
			"Tagは値を持たないAPIを使用してください");
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

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() != ComponentStorageKind::Tag,
			"Tagは値を持たないAPIを使用してください");
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
		return reinterpret_cast<const T*>(location.archetype->GetRaw(location.chunkIndex, location.row, typeID));
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
		void* ptr = location.archetype->GetRaw(location.chunkIndex, location.row, typeID);
		return DynamicBuffer<T>(static_cast<DynamicBufferHeader*>(ptr));
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::TryGetBuffer(const Entity& entity) {

		return HasBuffer<T>(entity) ? GetBuffer<T>(entity) : DynamicBuffer<T>{};
	}

	template <typename T>
	inline DynamicBuffer<const T> ECSWorld::GetBuffer(const Entity& entity) const {

		static_assert(ComponentTypeTraits::ResolveStorageKind<T>() == ComponentStorageKind::Buffer,
			"DynamicBuffer要素へkStorageKindを設定してください");
		AssertAlive(entity);
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		const auto& location = records_[entity.index].location;
		const EntityArchetype& archetype = *location.archetype;
		const void* storage = archetype.GetRaw(location.chunkIndex, location.row, typeID);
		return DynamicBuffer<const T>(static_cast<const DynamicBufferHeader*>(storage));
	}

	template <typename T>
	inline DynamicBuffer<const T> ECSWorld::TryGetBuffer(const Entity& entity) const {

		return HasBuffer<T>(entity) ? GetBuffer<T>(entity) : DynamicBuffer<const T>{};
	}

	template <typename T>
	inline std::span<const T> ECSWorld::GetBufferSpan(const Entity& entity) const {

		return TryGetBuffer<T>(entity).GetSpan();
	}

	template <typename T>
	inline void ECSWorld::SetComponentEnabled(const Entity& entity, bool enabled) {

		static_assert(ComponentTypeTraits::ResolveEnableable<T>(), "kEnableableがtrueのComponentだけ状態を変更できます");
		AssertAlive(entity);

		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		auto& location = records_[entity.index].location;
		const uint32_t columnIndex = location.archetype->GetColumnIndex(typeID);
		EntityChunk* chunk = &location.archetype->GetChunk(location.chunkIndex);
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
		return location.archetype->GetChunk(location.chunkIndex).IsEnabledByColumnIndex(columnIndex, location.row);
	}

	template <typename T>
	inline T* ECSWorld::TryGetComponentForBinding(const Entity& entity) {

		if (!IsAlive(entity) || IsPendingDestroy(entity)) {
			return nullptr;
		}
		if (T* component = TryGetComponent<T>(entity)) {
			return component;
		}
		return static_cast<T*>(TryGetPendingComponentData(entity, ComponentTypeRegistry::GetInstance().GetID<T>()));
	}

	template <typename T>
	inline const T* ECSWorld::TryGetComponentForBinding(const Entity& entity) const {

		if (!IsAlive(entity) || IsPendingDestroy(entity)) {
			return nullptr;
		}
		if (const T* component = TryGetComponent<T>(entity)) {
			return component;
		}
		return static_cast<const T*>(TryGetPendingComponentData(entity, ComponentTypeRegistry::GetInstance().GetID<T>()));
	}

	template <typename T>
	inline DynamicBuffer<const T> ECSWorld::TryGetBufferForBinding(const Entity& entity) const {

		if (!IsAlive(entity) || IsPendingDestroy(entity)) {
			return {};
		}
		if (auto buffer = TryGetBuffer<T>(entity); buffer.IsValid()) {
			return buffer;
		}
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		return DynamicBuffer<const T>(static_cast<const DynamicBufferHeader*>(TryGetPendingComponentData(entity, typeID)));
	}

	template <typename T>
	inline DynamicBuffer<T> ECSWorld::TryGetBufferForBinding(const Entity& entity) {

		if (!IsAlive(entity) || IsPendingDestroy(entity)) return {};
		if (auto buffer = TryGetBuffer<T>(entity); buffer.IsValid()) return buffer;
		const uint32_t typeID = ComponentTypeRegistry::GetInstance().GetID<T>();
		return DynamicBuffer<T>(static_cast<DynamicBufferHeader*>(TryGetPendingComponentData(entity, typeID)));
	}
}
