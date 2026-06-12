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

		// OnCollisionEnterを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionEnter(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionStayを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionStay(ECSWorld& world, SystemContext& context, const CollisionContact& collision);
		// OnCollisionExitを対象Entityのビヘイビアへ渡す
		static void DispatchCollisionExit(ECSWorld& world, SystemContext& context, const CollisionContact& collision);

		// Play中runtime Inspector用にBehaviorHandleからlive instanceの現在値を取得設定
		static nlohmann::json GetRuntimeSerializedState(BehaviorHandle handle);
		static void SetRuntimeSerializedField(BehaviorHandle handle, const std::string& fieldId, const nlohmann::json& value);

		// ScriptBehaviour.Enabled用にowner EntityとscriptSlotIDでruntime entryを特定
		static int32_t GetScriptEnabled(const Entity& owner, const UUID& scriptSlotID);
		static void SetScriptEnabled(const Entity& owner, const UUID& scriptSlotID, bool enabled);

		// participant cacheを再ソート
		static void InvalidateExecutionOrder();

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
		// participantキャッシュを作り直して安定ソートする、構造変更時のみ
		void RebuildParticipants(ECSWorld& world);
		// Pass2: activeなscriptのAwakeを全件実行
		void InvokePendingAwake(ECSWorld& world, SystemContext& context);
		// Pass3: OnEnable/OnDisableの遷移を全件反映
		void ApplyEnableTransitions(ECSWorld& world, SystemContext& context);
		// Pass5: Startを全件実行
		void InvokePendingStart(ECSWorld& world, SystemContext& context);

		// 衝突イベントを対象Entityのビヘイビアへ渡す
		void DispatchCollision(ECSWorld& world, SystemContext& context, const CollisionContact& collision, int32_t phase);
	};
} // Engine

