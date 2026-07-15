#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// json
#include <json.hpp>

// c++
#include <string>
#include <cstdint>

namespace Engine {

	// front
	struct CollisionContact;

	//============================================================================
	//	MonoBehavior class
	//	コンポーネントとして使用されるクラス
	//============================================================================
	class MonoBehavior {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MonoBehavior() = default;
		virtual ~MonoBehavior() = default;

		//============================================================================
		//	初期化メソッド
		//============================================================================
		virtual void Awake(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Start(ECSWorld&, const SystemContext&, const Entity&) {}

		//============================================================================
		//	エンティティの状態変化に伴うメソッド
		//============================================================================
		virtual void OnEnable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDisable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDestroy(ECSWorld&, const SystemContext&, const Entity&) {}

		//============================================================================
		//	更新メソッド
		//============================================================================
		virtual void FixedUpdate(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Update(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void LateUpdate(ECSWorld&, const SystemContext&, const Entity&) {}

		//============================================================================
		//	衝突メソッド
		//============================================================================
		// 衝突開始時に呼ばれる
		virtual void OnCollisionEnter(ECSWorld&, const SystemContext&, const CollisionContact&) {}
		// 衝突継続中に呼ばれる
		virtual void OnCollisionStay(ECSWorld&, const SystemContext&, const CollisionContact&) {}
		// 衝突終了時に呼ばれる
		virtual void OnCollisionExit(ECSWorld&, const SystemContext&, const CollisionContact&) {}

		//============================================================================
		//	アニメーションメソッド
		//============================================================================
		// アニメーションイベント発火時に呼ばれる
		virtual void OnAnimationEvent(ECSWorld&, const SystemContext&, const Entity&,
			const std::string&, float, int32_t, const std::string&) {}

		//============================================================================
		//	C#スクリプト用
		//============================================================================
		virtual void SetSerializedFields([[maybe_unused]] const nlohmann::json& serializedFields) {}

		// Play中runtime Inspector用でinstanceの現在値を{ fieldGuid: value }で返す
		// ネイティブMonoBehaviorは保存対象を持たないため既定で空
		virtual nlohmann::json GetRuntimeSerializedState() { return nlohmann::json::object(); }
		// runtime instanceの単一fieldを即時更新する、authoringへは保存しない
		virtual void SetRuntimeSerializedField([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const std::string& fieldID,
			[[maybe_unused]] const nlohmann::json& value) {}

		// 所属するScriptEntryのscriptSlotIDを渡しC#へ転送してEnabled制御のidentityにする
		// ネイティブMonoBehaviorは使わないため既定でno-op
		virtual void SetSlotID([[maybe_unused]] uint64_t scriptSlotID) {}

		// 実体のmanaged instance等を必要なら生成する、生成済み/不要ならtrueを返す
		// inactive hierarchyでもライフサイクル前に全件生成するために使う
		// ネイティブMonoBehaviorは自身が実体なので既定でtrue
		virtual bool EnsureInstance([[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity) { return true; }

		// callback内で回復不能な例外が発生したか
		// faulted状態のビヘイビアは以降のgameplay callbackを停止する
		// ネイティブMonoBehaviorは例外を境界越えしないため既定でfalse
		virtual bool IsFaulted() const { return false; }
	};
} // Engine
