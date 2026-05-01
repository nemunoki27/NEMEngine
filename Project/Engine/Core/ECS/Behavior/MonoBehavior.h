#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Context/SystemContext.h>

// json
#include <json.hpp>

namespace Engine {

	// front
	struct CollisionContact;

	//============================================================================
	//	MonoBehavior class
	//	コンポーネントとして使用されるクラス
	//============================================================================
	class MonoBehavior {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MonoBehavior() = default;
		virtual ~MonoBehavior() = default;

		//========================================================================
		//	初期化メソッド
		//========================================================================

		virtual void Awake(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Start(ECSWorld&, const SystemContext&, const Entity&) {}

		//========================================================================
		//	エンティティの状態変化に伴うメソッド
		//========================================================================

		virtual void OnEnable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDisable(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void OnDestroy(ECSWorld&, const SystemContext&, const Entity&) {}

		//========================================================================
		//	更新メソッド
		//========================================================================

		virtual void FixedUpdate(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void Update(ECSWorld&, const SystemContext&, const Entity&) {}
		virtual void LateUpdate(ECSWorld&, const SystemContext&, const Entity&) {}

		//========================================================================
		//	衝突メソッド
		//========================================================================

		// 衝突開始時に呼ばれる
		virtual void OnCollisionEnter(ECSWorld&, const SystemContext&, const CollisionContact&) {}
		// 衝突継続中に呼ばれる
		virtual void OnCollisionStay(ECSWorld&, const SystemContext&, const CollisionContact&) {}
		// 衝突終了時に呼ばれる
		virtual void OnCollisionExit(ECSWorld&, const SystemContext&, const CollisionContact&) {}

		//========================================================================
		//	C#スクリプト用
		//========================================================================

		virtual void SetSerializedFields([[maybe_unused]] const nlohmann::json& serializedFields) {}
	};
} // Engine
