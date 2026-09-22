#pragma once

//============================================================================
//	include
//============================================================================
#include "BehaviorExecutionSession.h"
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
		// Prefab生成中にScript実体とAwakeとOnEnableを返却前まで同期する
		static void SynchronizeInstantiatedEntities(ECSWorld& world, SystemContext& context,
			std::span<const Entity> entities);

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
		//--------- variables ----------------------------------------------------

		BehaviorExecutionSession session_;
		static BehaviorSystem* activeSystem_;
	};
} // Engine
