#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Behavior/World/BehaviorWorld.h>
#include "BehaviorRecordSynchronizer.h"
#include "BehaviorParticipantCache.h"
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Physics/Collision/CollisionTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	BehaviorExecutionSession class
	//	World内のScript実行状態と遷移を所有する
	//============================================================================
	class BehaviorExecutionSession {
		friend class BehaviorSystem;
	public:
		BehaviorExecutionSession() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 1回のSynchronizeで処理するscriptの安定スナップショット要素
		using SyncParticipant = BehaviorParticipantCache::SyncParticipant;

		//--------- variables ----------------------------------------------------

		ECSWorld* activeWorld_ = nullptr;
		BehaviorWorld runtime_;
		BehaviorParticipantCache participantCache_;

		// ScriptComponentが変更されたEntity、通知時に積んで同期前に重複除去する
		std::vector<Entity> dirtyScriptEntities_;
		// Active/Hierarchy変更後にOnEnable/OnDisableを再評価する
		bool enableTransitionsDirty_ = true;
		// 初回ロードとHot Reloadでだけ全Script同期を行う
		bool fullSyncRequested_ = true;
		// ECSWorldのComponent変更通知購読ID
		uint64_t componentMutationListenerID_ = 0;
		BehaviorRecordSynchronizer records_{ runtime_, dirtyScriptEntities_, participantCache_.participantsDirty_, enableTransitionsDirty_ };

		//--------- functions ----------------------------------------------------

		// アクティブなワールドを設定
		void EnsureActiveWorld(ECSWorld& world, SystemContext& context);
		// ワールド内のビヘイビアハンドルをリセット
		void ResetRuntimeState(ECSWorld& world);

		// 全パスをまとめて実行する、sweep時は参照されなくなったビヘイビアを破棄する
		void SynchronizeLifecycle(ECSWorld& world, SystemContext& context, bool sweep);

		// Component変更通知をDirty状態へ変換する
		static void OnComponentMutation(ECSWorld& world, const Entity& entity,
			uint32_t typeID, ComponentMutationKind kind, void* userData);

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
}
