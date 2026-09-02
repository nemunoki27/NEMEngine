#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/World/BehaviorWorld.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	BehaviorSystem class
	//	スクリプトからビヘイビアの実体化、ライフサイクルを管理するシステム
	//============================================================================
	class BehaviorSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BehaviorSystem() = default;
		~BehaviorSystem() = default;

		// ワールド切り替えで呼ばれる
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		// 各更新フェーズ
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		void Update(ECSWorld& world, SystemContext& context) override;
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		void OnSceneInstancesChanged(ECSWorld& world, SystemContext& context,
			SceneChangePhase phase) override;

		// OnCollisionEnterを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionEnter(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionStayを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionStay(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionExitを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionExit(ECSWorld& world, SystemContext& context, const CollisionContact& collision);

		// OnAnimationEventを対象Entityのビヘイビアへ渡す
		static void DispatchAnimationEvent(ECSWorld& world, SystemContext& context, const Entity& entity,
			const std::string& name, float floatParam, int32_t intParam, const std::string& stringParam);

		// Play中runtime Inspector用にBehaviorHandleからlive instanceの現在値を取得設定
		static nlohmann::json GetRuntimeSerializedState(BehaviorHandle handle);
		static void SetRuntimeSerializedField(BehaviorHandle handle, const std::string& fieldID, const nlohmann::json& value);

		// ScriptBehaviour.Enabled用にowner EntityとscriptSlotIDでruntime entryを特定
		static int32_t GetScriptEnabled(const Entity& owner, const UUID& scriptSlotID);
		static void SetScriptEnabled(const Entity& owner, const UUID& scriptSlotID, bool enabled);
		// Editorの実行時表示用にScript Slotへ対応するハンドルを返す
		static BehaviorHandle FindRuntimeHandle(const Entity& owner, const UUID& scriptSlotID);

		// GetComponent<Script>用にowner Entity上でscriptTypeID一致のscript instanceを返す、未解決はnullptr
		static MonoBehavior* FindScriptInstance(const Entity& owner, const std::string& scriptTypeID);

		// AddComponent<Script>用にowner EntityへscriptTypeIDのscriptをruntimeでattachする
		// instanceは即時生成しAwake/Startは次のライフサイクル同期で走る、生成成否を返す
		static bool AttachScript(const Entity& owner, const std::string& scriptTypeID);

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "BehaviorSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- types --------------------------------------------------------

		// 1回のSynchronizeで処理するscriptの安定スナップショット要素
		struct SyncParticipant {

			BehaviorHandle handle;
			Entity owner = Entity::Null();
			int32_t slot = 0;
			int32_t executionOrder = 0;
		};

		//--------- variables ----------------------------------------------------

		ECSWorld* activeWorld_ = nullptr;
		BehaviorWorld runtime_;
		static BehaviorSystem* activeSystem_;

		// ソート済みparticipantキャッシュと、その再構築要否
		std::vector<SyncParticipant> participants_;
		bool participantsDirty_ = true;
		// 同じフレームでUpdateを実行したparticipant
		std::vector<SyncParticipant> lateUpdateParticipants_;
		// ScriptComponentが変更されたEntity、通知時に積んで同期前に重複除去する
		std::vector<Entity> dirtyScriptEntities_;
		// Active/Hierarchy変更後にOnEnable/OnDisableを再評価する
		bool enableTransitionsDirty_ = true;
		// 初回ロードとHot Reloadでだけ全Script同期を行う
		bool fullSyncRequested_ = true;
		// ECSWorldのComponent変更通知購読ID
		uint64_t componentMutationListenerID_ = 0;

		//--------- functions ----------------------------------------------------

		// アクティブなワールドを設定
		void EnsureActiveWorld(ECSWorld& world, SystemContext& context);
		// ワールド内のビヘイビアハンドルをリセット
		void ResetRuntimeState(ECSWorld& world);

		//---------ライフサイクル同期複数パス----------------------------------

		// 全パスをまとめて実行する、sweep時は参照されなくなったビヘイビアを破棄する
		void SynchronizeLifecycle(ECSWorld& world, SystemContext& context, bool sweep);
		// Pass1: ScriptComponentを走査しrecord生成破棄・型解決・instance生成・serialized適用を行う
		void SynchronizeRecords(ECSWorld& world, SystemContext& context, bool sweep);
		// 変更通知されたEntityだけrecordを同期する
		void SynchronizeDirtyRecords(ECSWorld& world, SystemContext& context);
		// Entity1つ分のrecordを同期する
		void SynchronizeEntityRecords(ECSWorld& world, SystemContext& context,
			const Entity& entity, bool clearOwnerSeen);
		// Component変更通知をDirty状態へ変換する
		static void OnComponentMutation(ECSWorld& world, const Entity& entity,
			uint32_t typeID, ComponentMutationKind kind, void* userData);
		// Script変更Entityを次の同期へ積む
		void QueueScriptEntity(const Entity& entity);
		// participantキャッシュを作り直して安定ソートする、構造変更時のみ
		void RebuildParticipants(ECSWorld& world);
		// Pass2: activeなscriptのAwakeを全件実行
		void InvokePendingAwake(ECSWorld& world, SystemContext& context);
		// runtime設定を優先してscriptの有効状態を取得する
		bool IsParticipantEnabled(ECSWorld& world, const SyncParticipant& participant,
			const BehaviorRecord& record) const;
		// 現在のEntityとScript状態からコールバックを実行できるか確認する
		bool CanInvokeParticipant(ECSWorld& world, const SyncParticipant& participant,
			const BehaviorRecord& record) const;
		// コールバック後にハンドルからレコードを取り直して例外状態を反映する
		void RefreshFaultState(const BehaviorHandle& handle);
		// Active変更で発生したAwakeとOnEnableとOnDisableを安定するまで反映する
		void FlushActiveTransitions(ECSWorld& world, SystemContext& context);
		// ScriptまたはActive変更が残っていればライフサイクルを同期する
		void SynchronizeLifecycleIfDirty(ECSWorld& world, SystemContext& context);
		// Pass3: OnEnable/OnDisableの遷移を全件反映
		void ApplyEnableTransitions(ECSWorld& world, SystemContext& context);
		// Pass5: Startを全件実行し実行したものがあればtrueを返す
		bool InvokePendingStart(ECSWorld& world, SystemContext& context);

		// 衝突イベントを対象Entityのビヘイビアへ渡す
		void DispatchCollision(ECSWorld& world, SystemContext& context, const CollisionContact& collision, int32_t phase);
	};
} // Engine
